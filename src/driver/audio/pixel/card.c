/*
 * \brief  Sound-card side of the pixel_audio driver
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * The AoC sound card (aoc_alsa + CS35L41) is driven from inside the kernel
 * emulation through the file operations ALSA registers for its devices, as
 * repos/pc/src/driver/audio/pc does: control and PCM devices are opened with
 * a made-up inode/file pair, ioctls are called with kernel pointers.
 *
 * What this file does, in the order of the bring-up stages (lx_user.c):
 *
 *   card   register the sound card once the AoC is online
 *   mixer  enumerate all controls, read each once, generate the "mixer"
 *          report
 *   play   start the card task, which applies the mixer policy ('config'
 *          ROM, audio.h) and moves
 *          samples between the Record/Play sessions and the PCMs named there
 *
 * Reading most AoC controls sends a command to the AoC. Controls are
 * therefore read exactly once (for the report); afterwards the driver works
 * with the values it has written itself, as Android's audio_route does.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>
#include <lx_emul/task.h>

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/sched/task.h>
#include <linux/string.h>

#include <sound/asound.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>

/*
 * vendor headers: AoC services (aoc.h has no include guard, it comes with
 * aoc_alsa_drv.h) and the firmware interface
 */
#include <aoc_alsa_drv.h>
#include <linux/firmware.h>
#include <aoc-interface.h>

#include "audio.h"
#include "ctl_merge.h"

#define LOG(fmt, ...) printk("pixel_audio: " fmt "\n", ##__VA_ARGS__)

enum {
	MAX_ELEMS         = 512,   /* elements of one control (bytes: 512)        */
	MAX_REPORT_BYTES  = 64,    /* byte controls up to this size are read      */
	PERIOD_FRAMES     = PIXEL_AUDIO_PERIOD_FRAMES,
	FRAME_BYTES       = PIXEL_AUDIO_CHANNELS * 2,
	QUEUE_PERIODS     = 4,     /* periods kept queued in the playback PCM     */
	BUFFER_PERIODS    = 8,     /* buffer of a session-backed PCM, in periods  */
	STALL_MS          = 1000,  /* running PCM without a period event          */
	RETRY_MS          = 2000,  /* pause after a failed start of the playback  */
	MAX_STREAMS       = 8,     /* 'stream' PCMs held open                     */
	MAX_TUNINGS       = 16,
	TUNING_TIMEOUT_MS = 500,

	/*
	 * capture: level lines after each start, one per 100 ms of samples for
	 * the first two seconds (the shape of the start), then one per second
	 */
	PERIODS_PER_SECOND    = PIXEL_AUDIO_RATE / PIXEL_AUDIO_PERIOD_FRAMES,
	CAPTURE_LEVEL_SECONDS = 20,
	CAPTURE_FINE_SECONDS  = 2,
	CAPTURE_FINE_PERIODS  = PERIODS_PER_SECOND / 10,
};


/*******************************
 ** File operations of ALSA   **
 *******************************/

/*
 * sound/core/sound.c registers one set of file operations for the ALSA
 * major; its 'open' dispatches to the device behind the minor. The unit is
 * compiled with '__register_chrdev' renamed to this function (target.mk).
 */
static struct file_operations const *_alsa_fops;

int pixel_audio_register_chrdev(unsigned int major, unsigned int baseminor,
                                unsigned int count, const char *name,
                                const struct file_operations *fops)
{
	if (major == CONFIG_SND_MAJOR)
		_alsa_fops = fops;
	return 0;
}


/* fs/open.c: mark the file as a stream, nothing to seek */
int stream_open(struct inode *inode, struct file *filp)
{
	filp->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE |
	                  FMODE_ATOMIC_POS);
	filp->f_mode |= FMODE_STREAM;
	return 0;
}


int nonseekable_open(struct inode *inode, struct file *filp)
{
	filp->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	return 0;
}


loff_t noop_llseek(struct file *file, loff_t offset, int whence)
{
	return file->f_pos;
}


/*
 * An opened ALSA device
 */
struct handle
{
	struct file  file;
	struct inode inode;
	unsigned     device;     /* PCM number                      */
	int          capture;
	unsigned     periods;    /* period-elapsed events (fasync)  */
};


static struct handle *handle_open(dev_t devt, int *err)
{
	struct handle *h;

	*err = -ENODEV;

	if (!_alsa_fops || !_alsa_fops->open)
		return NULL;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h) {
		*err = -ENOMEM;
		return NULL;
	}

	h->inode.i_rdev = devt;
	h->file.f_inode = &h->inode;
	h->file.f_mode  = FMODE_READ | FMODE_WRITE;

	/* never sleep in open, read or write: the card task has one thread */
	h->file.f_flags = O_RDWR | O_NONBLOCK;

	*err = _alsa_fops->open(&h->inode, &h->file);
	if (*err) {
		kfree(h);
		return NULL;
	}
	return h;
}


static int handle_ioctl(struct handle *h, unsigned cmd, void *arg)
{
	if (!h || !h->file.f_op || !h->file.f_op->unlocked_ioctl)
		return -ENODEV;

	return (int)h->file.f_op->unlocked_ioctl(&h->file, cmd, (unsigned long)arg);
}


static void handle_close(struct handle *h)
{
	if (!h)
		return;

	if (h->file.f_op && h->file.f_op->fasync)
		h->file.f_op->fasync(-1, &h->file, 0);

	if (h->file.f_op && h->file.f_op->release)
		h->file.f_op->release(&h->inode, &h->file);

	kfree(h);
}


/**********
 ** Card **
 **********/

struct ctl
{
	struct snd_ctl_elem_info info;

	char     **items;         /* enum item names                             */
	long long *values;        /* value per element as read once or as        */
	                          /* written last, NULL if unknown               */
	int        read_error;
	bool       reported_unknown;
};

struct held_stream
{
	struct handle *h;
	unsigned       pcm;
	int            capture;
	unsigned       order;      /* position in the sequence of opens */
};

struct tuning { unsigned block, component, key, value; };

static struct
{
	struct snd_card *snd;

	/* mixer */
	struct handle *ctl_handle;
	struct ctl   **ctls;
	unsigned       num_ctls;
	unsigned       listed;             /* elements the card listed last time */
	unsigned       read_ok, read_failed, read_skipped;

	/* configuration */
	struct pixel_audio_plan const *plan;
	bool           baseline_done;
	unsigned       applied;            /* number of configs applied */
	struct held_stream streams[MAX_STREAMS];
	unsigned       stream_opens;
	struct tuning  tunings[MAX_TUNINGS];
	unsigned       num_tunings;

	/* statistics of the last config */
	unsigned       writes, unchanged, failed, unknown;

	/* session-backed playback */
	int            playback_pcm;       /* < 0: disabled */
	struct handle *playback;
	bool           playback_armed;     /* data may be available */
	int            playback_error;     /* last open/setup error */
	unsigned long  playback_retry_at;  /* jiffies, next attempt after an error */
	unsigned       playback_underruns;
	unsigned long  playback_periods;   /* periods written since the start */
	unsigned long  playback_event_jiffies;
	bool           playback_stall_reported;
	bool           playback_probing;   /* closed, armed, waiting for a sample */
	bool           playback_running_reported;
	unsigned       playback_starts;

	/* session-backed capture */
	int            capture_pcm;
	struct handle *capture;
	unsigned       capture_starts;
	unsigned long  capture_periods;    /* periods read since the start */
	unsigned       capture_overruns;   /* since the start */
	unsigned long  capture_event_jiffies;
	bool           capture_stall_reported;

	/* level of the current interval, per channel, in 16-bit sample values */
	int            capture_peak[PIXEL_AUDIO_CHANNELS];
	u64            capture_sum[PIXEL_AUDIO_CHANNELS];   /* of the magnitudes */
	s64            capture_dc[PIXEL_AUDIO_CHANNELS];    /* of the samples    */


	/* card task */
	struct task_struct *task;
	unsigned long       events;
} _card = { .playback_pcm = -1, .capture_pcm = -1 };


/*
 * Scheduling point for the forwarding of the AoC firmware log (aoc_log.c):
 * the vendor code does not sleep while it waits for the reply of an AoC
 * command, so the log thread runs only where the card code sleeps. Not
 * while a session-backed PCM streams: its periods must not wait for the
 * serial line, the log thread gets its turn whenever the card task sleeps.
 */
static void aoc_log_point(unsigned max_lines)
{
	if (_card.playback || _card.capture)
		return;

	pixel_audio_aoc_log_drain(max_lines, true);
}


/************
 ** Events **
 ************/

void pixel_audio_card_notify(enum pixel_audio_event event)
{
	set_bit(event, &_card.events);

	if (_card.task)
		lx_emul_task_unblock(_card.task);
}


/*
 * fs/fcntl.c: registration of a file for SIGIO. ALSA signals an elapsed
 * period through 'snd_kill_fasync(runtime->fasync, SIGIO, ...)', which ends
 * in 'kill_fasync' (from a work item). The file identifies the PCM.
 */
int fasync_helper(int fd, struct file *filp, int on, struct fasync_struct **fapp)
{
	if (!on) {
		kfree(*fapp);
		*fapp = NULL;
		return 0;
	}

	if (!*fapp) {
		*fapp = kzalloc(sizeof(**fapp), GFP_KERNEL);
		if (!*fapp)
			return -ENOMEM;
	}

	(*fapp)->fa_file = filp;
	return 1;
}


void kill_fasync(struct fasync_struct **fp, int sig, int band)
{
	struct file const *file = (fp && *fp) ? (*fp)->fa_file : NULL;

	if (!file)
		return;

	if (_card.playback && file == &_card.playback->file) {
		_card.playback->periods++;
		pixel_audio_card_notify(PIXEL_AUDIO_EVENT_PLAYBACK);
	}

	if (_card.capture && file == &_card.capture->file) {
		_card.capture->periods++;
		pixel_audio_card_notify(PIXEL_AUDIO_EVENT_CAPTURE);
	}
}


/***********
 ** Mixer **
 ***********/

static char const *ctl_type_name(int type)
{
	switch (type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:    return "bool";
	case SNDRV_CTL_ELEM_TYPE_INTEGER:    return "int";
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED: return "enum";
	case SNDRV_CTL_ELEM_TYPE_BYTES:      return "bytes";
	case SNDRV_CTL_ELEM_TYPE_IEC958:     return "iec958";
	case SNDRV_CTL_ELEM_TYPE_INTEGER64:  return "int64";
	}
	return "none";
}


static char const *ctl_iface_name(int iface)
{
	switch (iface) {
	case SNDRV_CTL_ELEM_IFACE_CARD:      return "card";
	case SNDRV_CTL_ELEM_IFACE_HWDEP:     return "hwdep";
	case SNDRV_CTL_ELEM_IFACE_MIXER:     return "mixer";
	case SNDRV_CTL_ELEM_IFACE_PCM:       return "pcm";
	case SNDRV_CTL_ELEM_IFACE_RAWMIDI:   return "rawmidi";
	case SNDRV_CTL_ELEM_IFACE_TIMER:     return "timer";
	case SNDRV_CTL_ELEM_IFACE_SEQUENCER: return "sequencer";
	}
	return "?";
}


static char const *ctl_name(struct ctl const *ctl)
{
	return (char const *)ctl->info.id.name;
}


/* largest number of elements the value ioctls can carry for a type */
static unsigned ctl_max_elems(int type)
{
	switch (type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED: return 128;
	case SNDRV_CTL_ELEM_TYPE_INTEGER64:  return 64;
	case SNDRV_CTL_ELEM_TYPE_BYTES:      return 512;
	}
	return 0;
}


static void ctl_values_from(struct ctl const *ctl,
                            struct snd_ctl_elem_value const *v, long long *out)
{
	unsigned i;

	for (i = 0; i < ctl->info.count; i++) {
		switch (ctl->info.type) {
		case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		case SNDRV_CTL_ELEM_TYPE_INTEGER:
			out[i] = v->value.integer.value[i]; break;
		case SNDRV_CTL_ELEM_TYPE_INTEGER64:
			out[i] = v->value.integer64.value[i]; break;
		case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
			out[i] = v->value.enumerated.item[i]; break;
		case SNDRV_CTL_ELEM_TYPE_BYTES:
			out[i] = v->value.bytes.data[i]; break;
		default:
			out[i] = 0;
		}
	}
}


static void ctl_values_to(struct ctl const *ctl, long long const *in,
                          struct snd_ctl_elem_value *v)
{
	unsigned i;

	for (i = 0; i < ctl->info.count; i++) {
		switch (ctl->info.type) {
		case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		case SNDRV_CTL_ELEM_TYPE_INTEGER:
			v->value.integer.value[i] = (long)in[i]; break;
		case SNDRV_CTL_ELEM_TYPE_INTEGER64:
			v->value.integer64.value[i] = in[i]; break;
		case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
			v->value.enumerated.item[i] = (unsigned)in[i]; break;
		case SNDRV_CTL_ELEM_TYPE_BYTES:
			v->value.bytes.data[i] = (unsigned char)in[i]; break;
		}
	}
}


/*
 * Read a control once. Byte controls beyond MAX_REPORT_BYTES (firmware
 * coefficients of the amplifiers) and write-only controls are not read.
 */
static void ctl_read_once(struct ctl *ctl)
{
	struct snd_ctl_elem_value *v;
	unsigned const count = ctl->info.count;
	unsigned const max   = ctl_max_elems(ctl->info.type);

	if (!(ctl->info.access & SNDRV_CTL_ELEM_ACCESS_READ) || !max
	 || count == 0 || count > max
	 || (ctl->info.type == SNDRV_CTL_ELEM_TYPE_BYTES
	  && count > MAX_REPORT_BYTES)) {
		_card.read_skipped++;
		return;
	}

	v = kzalloc(sizeof(*v), GFP_KERNEL);
	if (!v)
		return;

	v->id.numid = ctl->info.id.numid;

	ctl->read_error = handle_ioctl(_card.ctl_handle, SNDRV_CTL_IOCTL_ELEM_READ, v);

	/* what the firmware said about the commands of the reads so far */
	if (!_card.playback && !_card.capture)
		pixel_audio_aoc_log_drain(8, false);

	if (ctl->read_error) {
		_card.read_failed++;
	} else {
		ctl->values = kcalloc(count, sizeof(*ctl->values), GFP_KERNEL);
		if (ctl->values) {
			ctl_values_from(ctl, v, ctl->values);
			_card.read_ok++;
		}
	}
	kfree(v);
}


static void ctl_enum_items(struct ctl *ctl)
{
	unsigned const n = ctl->info.value.enumerated.items;
	unsigned i;

	ctl->items = kcalloc(n ? n : 1, sizeof(char *), GFP_KERNEL);
	if (!ctl->items)
		return;

	for (i = 0; i < n; i++) {
		struct snd_ctl_elem_info info = { };

		info.id.numid              = ctl->info.id.numid;
		info.value.enumerated.item = i;

		if (handle_ioctl(_card.ctl_handle, SNDRV_CTL_IOCTL_ELEM_INFO, &info))
			break;

		ctl->items[i] = kstrdup(info.value.enumerated.name, GFP_KERNEL);
	}
}


static struct ctl *ctl_by_numid(unsigned numid)
{
	unsigned i;

	for (i = 0; i < _card.num_ctls; i++)
		if (_card.ctls[i]->info.id.numid == numid)
			return _card.ctls[i];
	return NULL;
}


static struct ctl *ctl_by_name(char const *name)
{
	unsigned i;

	for (i = 0; i < _card.num_ctls; i++)
		if (strcmp(ctl_name(_card.ctls[i]), name) == 0)
			return _card.ctls[i];
	return NULL;
}


/**
 * Add the controls the card has and the mixer does not know yet
 *
 * Called at stage "mixer" and whenever the card's control count changed
 * (the amplifier firmware creates controls when it is loaded). Each new
 * control is read once.
 *
 * \return number of controls added, negative on error
 */
static int mixer_refresh(void)
{
	struct snd_ctl_elem_list list = { };
	struct snd_ctl_elem_id  *ids;
	struct ctl             **ctls;
	unsigned i, added = 0;
	int err;

	err = handle_ioctl(_card.ctl_handle, SNDRV_CTL_IOCTL_ELEM_LIST, &list);
	if (err)
		return err;

	if (!list.count)
		return 0;

	ids = kcalloc(list.count, sizeof(*ids), GFP_KERNEL);
	if (!ids)
		return -ENOMEM;

	list.space = list.count;
	list.pids  = ids;

	err = handle_ioctl(_card.ctl_handle, SNDRV_CTL_IOCTL_ELEM_LIST, &list);
	if (err) {
		kfree(ids);
		return err;
	}

	_card.listed = list.count;

	ctls = krealloc(_card.ctls, (_card.num_ctls + list.used)*sizeof(*ctls),
	                GFP_KERNEL);
	if (!ctls) {
		kfree(ids);
		return -ENOMEM;
	}
	_card.ctls = ctls;

	for (i = 0; i < list.used; i++) {
		struct ctl *ctl;

		if (ctl_by_numid(ids[i].numid))
			continue;

		ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
		if (!ctl)
			break;

		ctl->info.id.numid = ids[i].numid;

		err = handle_ioctl(_card.ctl_handle, SNDRV_CTL_IOCTL_ELEM_INFO,
		                   &ctl->info);
		if (err) {
			LOG("mixer: info of control %u failed: %d", ids[i].numid, err);
			kfree(ctl);
			continue;
		}

		if (ctl->info.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED)
			ctl_enum_items(ctl);

		ctl_read_once(ctl);

		_card.ctls[_card.num_ctls++] = ctl;
		added++;
	}

	kfree(ids);
	return added;
}


static void mixer_report(void)
{
	struct pixel_audio_ctl_report *r;
	unsigned i;

	r = kcalloc(_card.num_ctls ? _card.num_ctls : 1, sizeof(*r), GFP_KERNEL);
	if (!r)
		return;

	for (i = 0; i < _card.num_ctls; i++) {
		struct ctl const *ctl = _card.ctls[i];

		r[i].numid      = ctl->info.id.numid;
		r[i].name       = ctl_name(ctl);
		r[i].iface      = ctl_iface_name(ctl->info.id.iface);
		r[i].type       = ctl_type_name(ctl->info.type);
		r[i].count      = ctl->info.count;
		r[i].readable   = !!(ctl->info.access & SNDRV_CTL_ELEM_ACCESS_READ);
		r[i].writable   = !!(ctl->info.access & SNDRV_CTL_ELEM_ACCESS_WRITE);
		r[i].num_values = ctl->values ? ctl->info.count : 0;
		r[i].values     = ctl->values;
		r[i].read_error = ctl->read_error;

		switch (ctl->info.type) {
		case SNDRV_CTL_ELEM_TYPE_INTEGER:
			r[i].min = ctl->info.value.integer.min;
			r[i].max = ctl->info.value.integer.max;
			break;
		case SNDRV_CTL_ELEM_TYPE_INTEGER64:
			r[i].min = ctl->info.value.integer64.min;
			r[i].max = ctl->info.value.integer64.max;
			break;
		case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
			r[i].num_items = ctl->items ? ctl->info.value.enumerated.items : 0;
			r[i].items     = (char const * const *)ctl->items;
			break;
		}
	}

	pixel_audio_report_mixer(_card.snd ? _card.snd->longname : "",
	                         r, _card.num_ctls);
	kfree(r);
}


/*
 * The amplifier firmware (wm_adsp) adds its coefficient controls from a
 * work item after it was loaded: pick them up when the card's control count
 * differs from what the mixer knows.
 */
static void mixer_check_new_controls(void)
{
	int added;

	if (!_card.snd || !_card.ctl_handle
	 || (unsigned)_card.snd->controls_count == _card.listed)
		return;

	added = mixer_refresh();
	if (added > 0) {
		LOG("mixer: %d new controls (%u in total), report updated",
		    added, _card.num_ctls);
		mixer_report();
	}
}


/********************************
 ** Control writes from config **
 ********************************/

/*
 * The rules that turn the value text of a node into element values are in
 * ctl_merge.h (also built into the host test, test/).
 */
static int merge_write(struct ctl const *ctl, struct pixel_audio_write const *w,
                       long long *vals)
{
	struct ctl_merge_ctl const m = {
		.type      = (ctl->info.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED) ? CTL_MERGE_ENUM
		           : (ctl->info.type == SNDRV_CTL_ELEM_TYPE_BYTES)      ? CTL_MERGE_BYTES
		           : (ctl->info.type == SNDRV_CTL_ELEM_TYPE_BOOLEAN)    ? CTL_MERGE_BOOL
		           :                                                      CTL_MERGE_INT,
		.count     = ctl->info.count,
		.items     = (char const * const *)ctl->items,
		.num_items = (ctl->info.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED)
		           ? ctl->info.value.enumerated.items : 0,
	};

	return ctl_merge_write(&m, w->index, w->value, vals);
}


static void format_values(char *buf, size_t size, long long const *vals,
                          unsigned count)
{
	unsigned i, n = 0;

	buf[0] = 0;
	for (i = 0; i < count && i < 8 && n < size; i++)
		n += scnprintf(buf + n, size - n, "%s%lld", i ? " " : "", vals[i]);

	if (count > 8 && n < size)
		scnprintf(buf + n, size - n, " ... (%u)", count);
}


/**
 * Apply a group of adjacent writes of one control as a single write
 *
 * \param defaults  the group consists of 'default' nodes: elements it does
 *                  not list keep their known value. For 'control' nodes
 *                  they become zero.
 * \param skip_equal  do not write if the control is known to hold the
 *                    values already
 * \return number of nodes consumed (>= 1)
 */
static unsigned apply_group(struct pixel_audio_write const *w, unsigned avail,
                            bool defaults, bool skip_equal, char const *reason)
{
	struct snd_ctl_elem_value *v = NULL;
	struct ctl *ctl;
	long long  *vals = NULL;
	unsigned    n = 1, i, count;
	char        text[96];
	int         err;

	while (n < avail && strcmp(w[n].name, w[0].name) == 0)
		n++;

	ctl = ctl_by_name(w->name);
	if (!ctl) {
		/* the amplifier firmware may have added controls meanwhile */
		mixer_check_new_controls();
		ctl = ctl_by_name(w->name);
	}

	if (!ctl) {
		_card.unknown++;
		LOG("WARNING: %s: no control \"%s\" on the card, skipped", reason,
		    w->name);
		return n;
	}

	count = ctl->info.count;

	if (!count || count > ctl_max_elems(ctl->info.type)
	 || !(ctl->info.access & SNDRV_CTL_ELEM_ACCESS_WRITE)) {
		_card.failed++;
		LOG("WARNING: %s: control \"%s\" (%s, %u elements) cannot be "
		    "written, skipped", reason, w->name,
		    ctl_type_name(ctl->info.type), count);
		return n;
	}

	vals = kcalloc(count, sizeof(*vals), GFP_KERNEL);
	v    = kzalloc(sizeof(*v), GFP_KERNEL);
	if (!vals || !v)
		goto out;

	if (defaults && ctl->values)
		memcpy(vals, ctl->values, count*sizeof(*vals));

	for (i = 0; i < n; i++) {
		err = merge_write(ctl, &w[i], vals);
		if (err) {
			_card.failed++;
			LOG("WARNING: %s: control \"%s\" (%s, %u elements): value "
			    "\"%s\"%s not usable (%d: -1 value, -2 index), control skipped", reason,
			    w->name, ctl_type_name(ctl->info.type), count, w[i].value,
			    w[i].index >= 0 ? " for an element" : "", err);
			goto out;
		}
	}

	format_values(text, sizeof(text), vals, count);

	if (skip_equal && ctl->values
	 && memcmp(ctl->values, vals, count*sizeof(*vals)) == 0) {
		_card.unchanged++;
		goto out;
	}

	v->id.numid = ctl->info.id.numid;
	ctl_values_to(ctl, vals, v);

	err = handle_ioctl(_card.ctl_handle, SNDRV_CTL_IOCTL_ELEM_WRITE, v);
	if (err < 0) {
		_card.failed++;
		LOG("WARNING: %s: write of \"%s\" = %s failed: %d", reason, w->name,
		    text, err);
		goto out;
	}

	_card.writes++;

	if (pixel_audio_config.verbose)
		LOG("%s: \"%s\" = %s", reason, w->name, text);

	/* the firmware's lines about this write follow it in the log */
	aoc_log_point(8);

	if (!ctl->values)
		ctl->values = kcalloc(count, sizeof(*vals), GFP_KERNEL);
	if (ctl->values)
		memcpy(ctl->values, vals, count*sizeof(*vals));
out:
	kfree(vals);
	kfree(v);
	return n;
}


/*****************
 ** PCM devices **
 *****************/

static struct snd_pcm_str *pcm_stream(unsigned device, int capture)
{
	struct snd_device *dev;
	int const dir = capture ? SNDRV_PCM_STREAM_CAPTURE
	                        : SNDRV_PCM_STREAM_PLAYBACK;

	list_for_each_entry(dev, &_card.snd->devices, list) {
		struct snd_pcm *pcm;

		if (dev->type != SNDRV_DEV_PCM)
			continue;

		pcm = dev->device_data;
		if (pcm->internal || pcm->device != device)
			continue;

		if (pcm->streams[dir].substream)
			return &pcm->streams[dir];
	}
	return NULL;
}


static void param_mask(struct snd_pcm_hw_params *p, unsigned index, unsigned bit)
{
	struct snd_mask *m = &p->masks[index - SNDRV_PCM_HW_PARAM_FIRST_MASK];

	memset(m->bits, 0, sizeof(m->bits));
	m->bits[bit >> 5] = 1u << (bit & 31);
	p->rmask |= 1u << index;
}


static void param_interval(struct snd_pcm_hw_params *p, unsigned index,
                           unsigned value)
{
	struct snd_interval *i =
		&p->intervals[index - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];

	i->min = i->max = value;
	i->openmin = i->openmax = 0;
	i->integer = 1;
	p->rmask |= 1u << index;
}


static void param_any(struct snd_pcm_hw_params *p)
{
	unsigned k;

	memset(p, 0, sizeof(*p));

	for (k = SNDRV_PCM_HW_PARAM_FIRST_MASK;
	     k <= SNDRV_PCM_HW_PARAM_LAST_MASK; k++) {
		memset(p->masks[k - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits, 0xff,
		       sizeof(p->masks[0].bits));
		p->rmask |= 1u << k;
	}

	for (k = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;
	     k <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; k++) {
		struct snd_interval *i =
			&p->intervals[k - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
		i->min = 0;
		i->max = ~0u;
		p->rmask |= 1u << k;
	}
}


/*
 * 48 kHz, two channels, signed 16 bit, interleaved read/write. With
 * 'with_period' the period is fixed to PERIOD_FRAMES. 'buffer_frames' fixes
 * the buffer size; 0 leaves it to ALSA (the largest the device allows).
 */
static int pcm_hw_params(struct handle *h, bool with_period,
                         unsigned buffer_frames)
{
	struct snd_pcm_hw_params *p = kzalloc(sizeof(*p), GFP_KERNEL);
	int err;

	if (!p)
		return -ENOMEM;

	param_any(p);
	param_mask(p, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
	param_mask(p, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
	param_mask(p, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
	param_interval(p, SNDRV_PCM_HW_PARAM_RATE,     PIXEL_AUDIO_RATE);
	param_interval(p, SNDRV_PCM_HW_PARAM_CHANNELS, PIXEL_AUDIO_CHANNELS);
	if (with_period)
		param_interval(p, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, PERIOD_FRAMES);
	if (buffer_frames)
		param_interval(p, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, buffer_frames);

	err = handle_ioctl(h, SNDRV_PCM_IOCTL_HW_PARAMS, p);

	kfree(p);
	return err;
}


/**
 * Open a PCM device and bring it to the PREPARED state
 *
 * This is what makes the kernel emit the AoC commands of a stream: open
 * (service allocation), hw_params, prepare (EP setup, voice-call start for
 * the voice PCMs, back-end and amplifier power-up through DAPM).
 */
static struct handle *pcm_open_prepared(unsigned device, int capture,
                                        unsigned buffer_frames, int *err)
{
	struct snd_pcm_str *str = pcm_stream(device, capture);
	struct handle      *h;
	char const         *step = "open";

	if (!str) {
		*err = -ENOENT;
		LOG("PCM %u (%s): no such device on the card", device,
		    capture ? "capture" : "playback");
		return NULL;
	}

	h = handle_open(str->dev.devt, err);
	if (!h)
		goto failed;

	h->device  = device;
	h->capture = capture;

	step = "hw_params";
	*err = pcm_hw_params(h, true, buffer_frames);
	if (*err) {
		LOG("PCM %u (%s): hw_params with a %u-frame period and a %u-frame "
		    "buffer (0 = any) failed (%d), retrying with rate, channels and "
		    "format only", device, capture ? "capture" : "playback",
		    PERIOD_FRAMES, buffer_frames, *err);
		*err = pcm_hw_params(h, false, 0);
	}
	if (*err)
		goto failed;

	step = "prepare";
	*err = handle_ioctl(h, SNDRV_PCM_IOCTL_PREPARE, NULL);
	if (*err)
		goto failed;

	return h;

failed:
	LOG("PCM %u (%s): %s failed: %d", device,
	    capture ? "capture" : "playback", step, *err);
	handle_close(h);
	return NULL;
}


static int pcm_status(struct handle *h, struct snd_pcm_status64 *status)
{
	memset(status, 0, sizeof(*status));
	return handle_ioctl(h, SNDRV_PCM_IOCTL_STATUS64, status);
}


/* the runtime of an opened PCM (sound/core/pcm_native.c, snd_pcm_open_file) */
static struct snd_pcm_runtime *pcm_runtime(struct handle *h)
{
	struct snd_pcm_file *pcm_file = h ? h->file.private_data : NULL;

	return (pcm_file && pcm_file->substream) ? pcm_file->substream->runtime
	                                         : NULL;
}


/*************************
 ** Streams held open   **
 *************************/

static struct held_stream *stream_lookup(unsigned pcm, int capture)
{
	unsigned i;

	for (i = 0; i < MAX_STREAMS; i++)
		if (_card.streams[i].h && _card.streams[i].pcm == pcm
		 && !_card.streams[i].capture == !capture)
			return &_card.streams[i];
	return NULL;
}


static void stream_hold(unsigned pcm, int capture)
{
	unsigned i;
	int err = 0;

	if (stream_lookup(pcm, capture))
		return;

	for (i = 0; i < MAX_STREAMS && _card.streams[i].h; i++) ;

	if (i == MAX_STREAMS) {
		LOG("WARNING: config: more than %u streams, PCM %u not opened",
		    MAX_STREAMS, pcm);
		return;
	}

	/* no data: the period is fixed, the buffer is whatever the device has */
	_card.streams[i].h = pcm_open_prepared(pcm, capture, 0, &err);
	if (!_card.streams[i].h) {
		_card.failed++;
		LOG("WARNING: config: stream PCM %u (%s) not opened: %d", pcm,
		    capture ? "capture" : "playback", err);
		return;
	}

	_card.streams[i].pcm     = pcm;
	_card.streams[i].capture = capture;
	_card.streams[i].order   = ++_card.stream_opens;

	LOG("config: stream PCM %u (%s) open and prepared", pcm,
	    capture ? "capture" : "playback");

	aoc_log_point(32);
}


static bool plan_has_stream(struct pixel_audio_plan const *plan, unsigned pcm,
                            int capture)
{
	unsigned i;

	for (i = 0; plan && i < plan->num_actions; i++) {
		struct pixel_audio_action const *a = &plan->actions[i];

		if (a->type == PIXEL_AUDIO_ACTION_STREAM && a->pcm == pcm
		 && !a->capture == !capture)
			return true;
	}
	return false;
}


/* close the streams the new plan does not name, last opened first */
static void streams_release(struct pixel_audio_plan const *plan)
{
	for (;;) {
		struct held_stream *last = NULL;
		unsigned i;

		for (i = 0; i < MAX_STREAMS; i++) {
			struct held_stream *s = &_card.streams[i];

			if (s->h && !plan_has_stream(plan, s->pcm, s->capture)
			 && (!last || s->order > last->order))
				last = s;
		}

		if (!last)
			return;

		handle_close(last->h);
		last->h = NULL;

		LOG("config: stream PCM %u (%s) closed", last->pcm,
		    last->capture ? "capture" : "playback");
	}
}


/************************
 ** AoC tuning service **
 ************************/

/*
 * The Android audio HAL sets the call volume through the character device
 * of the AoC service "audio_output_tuning" (/dev/acd-audio_output_tuning,
 * aoc_char_dev.c): it writes a CMD_AUDIO_OUTPUT_SET_PARAMETER and reads the
 * reply. aoc_char_dev.c is not built; the same two calls of the service API
 * are made here. Unlike aoc_alsa's own command channel, a missing reply
 * does not trigger the AoC watchdog (the HAL cannot do that either).
 */
static struct aoc_service_dev *aoc_service_by_name(char const *name)
{
	extern struct platform_device *aoc_platform_device;

	struct aoc_prvdata *prvdata;
	int i, n;

	if (!aoc_platform_device)
		return NULL;

	prvdata = platform_get_drvdata(aoc_platform_device);
	if (!prvdata)
		return NULL;

	n = aoc_num_services();
	for (i = 0; i < n; i++) {
		struct aoc_service_dev *dev = service_dev_at_index(prvdata, i);

		if (dev && strcmp(dev_name(&dev->dev), name) == 0)
			return dev;
	}
	return NULL;
}


static struct aoc_service_dev *tuning_service(void)
{
	return aoc_service_by_name("audio_output_tuning");
}


static int tuning_send(struct tuning const *t)
{
	enum { REPLY_SIZE = 1024 };   /* as aoc_audio_control of aoc_alsa_hw.c */

	struct CMD_AUDIO_OUTPUT_SET_PARAMETER cmd = { };
	struct aoc_service_dev *dev = tuning_service();
	uint8_t *reply;
	ssize_t  n;
	int      result = 0;
	long const timeout = msecs_to_jiffies(TUNING_TIMEOUT_MS);

	if (!dev) {
		LOG("WARNING: tuning: AoC service \"audio_output_tuning\" not found");
		return -ENODEV;
	}

	reply = kzalloc(REPLY_SIZE, GFP_KERNEL);
	if (!reply)
		return -ENOMEM;

	AocCmdHdrSet(&cmd.parent, CMD_AUDIO_OUTPUT_SET_PARAMETER_ID, sizeof(cmd));
	cmd.block     = (uint8_t)t->block;
	cmd.component = (uint8_t)t->component;
	cmd.key       = t->key;
	cmd.val       = t->value;

	/* drop replies nobody collected */
	while (aoc_service_read(dev, reply, REPLY_SIZE, false) >= 1) ;

	n = aoc_service_write_timeout(dev, (uint8_t const *)&cmd, sizeof(cmd),
	                              timeout);
	if (n != sizeof(cmd)) {
		LOG("WARNING: tuning: block %u component %u key %u value %u not "
		    "sent: %zd", t->block, t->component, t->key, t->value, n);
		result = n < 0 ? (int)n : -EIO;
		goto out;
	}

	n = aoc_service_read_timeout(dev, reply, REPLY_SIZE, timeout);
	if (n < (ssize_t)sizeof(struct CMD_HDR)) {
		LOG("WARNING: tuning: block %u component %u key %u value %u sent, "
		    "no reply within %u ms (%zd)", t->block, t->component, t->key,
		    t->value, TUNING_TIMEOUT_MS, n);
		result = n < 0 ? (int)n : -EIO;
		goto out;
	}

	LOG("tuning: block %u component %u key %u value %u sent, reply code %d "
	    "(%zd bytes)", t->block, t->component, t->key, t->value,
	    ((struct CMD_HDR const *)reply)->reply, n);
out:
	kfree(reply);
	aoc_log_point(8);
	return result;
}


/*
 * Voice tuning data
 *
 * The AoC's voice processor (Fortemedia) mutes both directions of a call
 * until it got its configuration. The Android audio HAL uploads it at the
 * start of a call and at every change of the output device: the parameter
 * 19/31/0 = 0, the volume 19/3/18 = 0, four data blocks, the volume again
 * and 19/31/1 = 0. A block is written to the AoC service "audio_bulk_tx"
 * (a buffer of 16384 bytes) and announced by a
 * CMD_AUDIO_OUTPUT_SET_PARAMETER_BULK on "audio_output_tuning". The
 * blocks are the vendor's data, recorded on LineageOS for one band (WB) and
 * delivered as ROM modules "voice_tuning.<route>.<n>"
 * (tool/voice_tuning_from_strace.py). Without them the call stays silent.
 */
enum { VOICE_TUNING_BLOCKS = 4, BULK_BUFFER_SIZE = 16384,
       VOICE_BLOCK = 19, VOICE_COMPONENT = 3, VOICE_KEY_VOLUME = 18,
       VOICE_COMPONENT_PROCESSING = 31,
       VOICE_RX_PCM = 4 /* opening it is aoc_alsa's "Start voice call" */ };


static int tuning_send_bulk(unsigned block, unsigned component,
                            uint8_t const *data, size_t size)
{
	enum { REPLY_SIZE = 1024 };

	struct CMD_AUDIO_OUTPUT_SET_PARAMETER_BULK cmd = { };
	struct aoc_service_dev *tuning = tuning_service();
	struct aoc_service_dev *bulk   = aoc_service_by_name("audio_bulk_tx");
	uint8_t *buffer, *reply;
	ssize_t  n;
	int      result = 0;
	long const timeout = msecs_to_jiffies(TUNING_TIMEOUT_MS);

	if (!tuning || !bulk) {
		LOG("WARNING: tuning: AoC service \"%s\" not found",
		    tuning ? "audio_bulk_tx" : "audio_output_tuning");
		return -ENODEV;
	}

	if (size > BULK_BUFFER_SIZE)
		return -EINVAL;

	buffer = kzalloc(BULK_BUFFER_SIZE, GFP_KERNEL);
	reply  = kzalloc(REPLY_SIZE, GFP_KERNEL);
	if (!buffer || !reply) {
		result = -ENOMEM;
		goto out;
	}
	memcpy(buffer, data, size);

	n = aoc_service_write_timeout(bulk, buffer, BULK_BUFFER_SIZE, timeout);
	if (n != BULK_BUFFER_SIZE) {
		LOG("WARNING: tuning: bulk data of %zu bytes not written: %zd",
		    size, n);
		result = n < 0 ? (int)n : -EIO;
		goto out;
	}

	AocCmdHdrSet(&cmd.parent, CMD_AUDIO_OUTPUT_SET_PARAMETER_BULK_ID,
	             sizeof(cmd));
	cmd.block     = (uint8_t)block;
	cmd.component = (uint8_t)component;
	cmd.tx_size   = (uint32_t)size;

	while (aoc_service_read(tuning, reply, REPLY_SIZE, false) >= 1) ;

	n = aoc_service_write_timeout(tuning, (uint8_t const *)&cmd, sizeof(cmd),
	                              timeout);
	if (n != sizeof(cmd)) {
		LOG("WARNING: tuning: bulk command for %zu bytes not sent: %zd",
		    size, n);
		result = n < 0 ? (int)n : -EIO;
		goto out;
	}

	n = aoc_service_read_timeout(tuning, reply, REPLY_SIZE, timeout);
	if (n < (ssize_t)sizeof(struct CMD_HDR)) {
		LOG("WARNING: tuning: bulk command for %zu bytes sent, no reply "
		    "within %u ms (%zd)", size, TUNING_TIMEOUT_MS, n);
		result = n < 0 ? (int)n : -EIO;
		goto out;
	}

	LOG("tuning: block %u component %u bulk data of %zu bytes sent, reply "
	    "code %d", block, component, size,
	    ((struct CMD_HDR const *)reply)->reply);
out:
	kfree(buffer);
	kfree(reply);
	return result;
}


/*
 * Upload the voice processor's configuration for a call route
 *
 * \param volume  the call volume step of the config (tuning 19/3/18)
 */
static void voice_tuning_upload(char const *route, unsigned volume)
{
	const struct firmware *fw[VOICE_TUNING_BLOCKS] = { };
	struct tuning t;
	char   name[48];
	unsigned i;
	int err = 0;

	for (i = 0; i < VOICE_TUNING_BLOCKS; i++) {
		snprintf(name, sizeof(name), "voice_tuning.%s.%u", route, i);
		if (firmware_request_nowarn(&fw[i], name, NULL) || !fw[i]) {
			LOG("WARNING: voice tuning: no ROM \"%s\": the AoC's voice "
			    "processor keeps its default configuration and the call "
			    "stays SILENT in both directions", name);
			goto release;
		}
	}

	t = (struct tuning){ VOICE_BLOCK, VOICE_COMPONENT_PROCESSING, 0, 0 };
	tuning_send(&t);
	t = (struct tuning){ VOICE_BLOCK, VOICE_COMPONENT, VOICE_KEY_VOLUME, 0 };
	tuning_send(&t);

	for (i = 0; i < VOICE_TUNING_BLOCKS && !err; i++) {

		/* a block states its own length (a ROM is page-rounded) */
		size_t const size = fw[i]->size >= 4
		                  ? (size_t)fw[i]->data[2] | ((size_t)fw[i]->data[3] << 8)
		                  : 0;

		if (size < 4 || size > fw[i]->size || size > BULK_BUFFER_SIZE) {
			LOG("WARNING: voice tuning: block %u of route \"%s\" states "
			    "%zu bytes, the ROM has %zu: not sent", i, route, size,
			    fw[i]->size);
			err = -EINVAL;
			break;
		}
		err = tuning_send_bulk(VOICE_BLOCK, VOICE_COMPONENT, fw[i]->data, size);
	}

	t = (struct tuning){ VOICE_BLOCK, VOICE_COMPONENT, VOICE_KEY_VOLUME, volume };
	tuning_send(&t);
	t = (struct tuning){ VOICE_BLOCK, VOICE_COMPONENT_PROCESSING, 1, 0 };
	tuning_send(&t);

	LOG("voice tuning: route \"%s\" %s, volume %u", route,
	    err ? "upload FAILED" : "uploaded", volume);

release:
	for (i = 0; i < VOICE_TUNING_BLOCKS; i++)
		release_firmware(fw[i]);

	aoc_log_point(40);
}


/*************************
 ** Session-backed PCMs **
 *************************/

static void playback_close(char const *why)
{
	struct handle *h = _card.playback;

	if (!h)
		return;

	/* period events that are still on their way find no playback file */
	_card.playback = NULL;

	handle_ioctl(h, SNDRV_PCM_IOCTL_DROP, NULL);
	handle_close(h);

	LOG("playback: PCM %d stopped and closed (%s), %lu periods written",
	    _card.playback_pcm, why, _card.playback_periods);
}


static int playback_write(short const *frames)
{
	struct snd_xferi xfer = { };

	xfer.buf    = (void *)frames;
	xfer.frames = PERIOD_FRAMES;

	return handle_ioctl(_card.playback, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xfer);
}


static void playback_failed(int err)
{
	_card.playback_error    = err;
	_card.playback_retry_at = jiffies + msecs_to_jiffies(RETRY_MS);
}


/* the mixer hands out exact zeros while its inputs have nothing */
static bool all_zero(short const *frames)
{
	unsigned i;

	for (i = 0; i < PERIOD_FRAMES*PIXEL_AUDIO_CHANNELS; i++)
		if (frames[i])
			return false;
	return true;
}


/* a write or read found the stream in XRUN: prepare it again */
static int playback_recover(char const *where)
{
	int err;

	if (++_card.playback_underruns <= 5 || _card.playback_underruns % 100 == 0)
		LOG("playback: underrun %u (%s), restarting",
		    _card.playback_underruns, where);

	err = handle_ioctl(_card.playback, SNDRV_PCM_IOCTL_PREPARE, NULL);
	if (err)
		LOG("playback: prepare after underrun failed: %d", err);
	return err;
}


/* true if another period fits and is wanted */
static bool playback_wants_data(void)
{
	struct snd_pcm_status64 status;
	int err;

	err = pcm_status(_card.playback, &status);
	if (err) {
		LOG("playback: status failed: %d", err);
		return false;
	}

	/* logged for the first few, then every 100th: it can come per period */
	if (status.state == SNDRV_PCM_STATE_XRUN)
		return playback_recover("status") == 0;

	if (status.state == SNDRV_PCM_STATE_PREPARED)
		return status.avail >= PERIOD_FRAMES
		    && status.appl_ptr - status.hw_ptr < QUEUE_PERIODS*PERIOD_FRAMES;

	if (status.state != SNDRV_PCM_STATE_RUNNING)
		return false;

	return status.appl_ptr - status.hw_ptr < QUEUE_PERIODS*PERIOD_FRAMES
	    && status.avail >= PERIOD_FRAMES;
}


/*
 * Feed the playback PCM from the Record sessions. The PCM is opened when
 * samples arrive and closed when the sessions are depleted, so that the
 * back end and with it the amplifiers power down while nothing plays.
 *
 * What the mixer does (record_play_mixer, record_session.h): a 'record'
 * without input returns cleared samples for 250 ms and then "depleted"
 * once; a wake-up signal is sent for every period of a playing client
 * while nobody records, and once when the handler is installed. So, while
 * the PCM is closed, a period is recorded per tick and thrown away until
 * one holds a non-zero sample (start) or the mixer reports depletion
 * (wait for the next wake-up). While the PCM runs, depletion closes it:
 * the mixer has delivered 250 ms of silence by then.
 */
static void playback_service(void)
{
	short const *frames;
	unsigned     written = 0;
	int          err;

	_card.playback_probing = false;

	if (_card.playback_pcm < 0)
		return;

	if (!_card.playback) {

		if (!_card.playback_armed)
			return;

		/* a start that failed is not repeated with every wake-up */
		if (_card.playback_error
		 && time_before(jiffies, _card.playback_retry_at))
			return;

		frames = pixel_audio_record_period();
		if (!frames) {
			_card.playback_armed = false;   /* wait for the wake-up signal */
			return;
		}

		if (all_zero(frames)) {
			_card.playback_probing = true;  /* look again in one period */
			return;
		}

		_card.playback = pcm_open_prepared(_card.playback_pcm, 0,
		                                   BUFFER_PERIODS*PERIOD_FRAMES, &err);
		if (!_card.playback) {
			playback_failed(err);
			LOG("playback: PCM %d not started: %d (next attempt in %u ms "
			    "or with the next config)", _card.playback_pcm, err,
			    RETRY_MS);
			return;
		}

		/* period events of this file wake the card task */
		if (_card.playback->file.f_op->fasync)
			_card.playback->file.f_op->fasync(-1, &_card.playback->file, 1);

		_card.playback_error          = 0;
		_card.playback_periods        = 0;
		_card.playback_stall_reported = false;
		_card.playback_event_jiffies  = jiffies;
		_card.playback_starts++;

		err = playback_write(frames);
		if (err) {
			LOG("playback: first write failed: %d", err);
			playback_close("write error");
			playback_failed(err);
			return;
		}
		_card.playback_periods++;
	}

	while (written++ < 2*QUEUE_PERIODS && playback_wants_data()) {

		frames = pixel_audio_record_period();
		if (!frames) {
			playback_close("Record sessions depleted");
			_card.playback_armed = false;
			return;
		}

		err = playback_write(frames);
		if (err == -EAGAIN)
			return;

		/*
		 * The status ioctl samples the state before it updates the
		 * pointer: an underrun it detects itself is seen by the write only
		 */
		if (err == -EPIPE) {
			if (playback_recover("write"))
				return;
			continue;
		}

		if (err) {
			/* not per period: stop, the start is retried after RETRY_MS */
			LOG("playback: write failed: %d", err);
			playback_close("write error");
			playback_failed(err);
			return;
		}
		_card.playback_periods++;
	}
}


static void playback_check_stall(void)
{
	struct snd_pcm_status64 status;

	if (!_card.playback || _card.playback_stall_reported
	 || time_before(jiffies, _card.playback_event_jiffies
	                         + msecs_to_jiffies(STALL_MS)))
		return;

	_card.playback_stall_reported = true;

	if (pcm_status(_card.playback, &status))
		return;

	LOG("WARNING: playback: PCM %d got no period event for %u ms: state %d "
	    "(2 prepared, 3 running, 4 xrun), hw_ptr %lu, appl_ptr %lu, %lu "
	    "periods written, %u period events", _card.playback_pcm, STALL_MS,
	    status.state, status.hw_ptr, status.appl_ptr,
	    _card.playback_periods, _card.playback->periods);
}


/*
 * Log once per boot that the playback PCM advances, after four period events
 */
static void playback_report_running(void)
{
	struct snd_pcm_status64 status = { };

	if (_card.playback_running_reported || !_card.playback
	 || _card.playback->periods < 4)
		return;

	_card.playback_running_reported = true;

	pcm_status(_card.playback, &status);

	LOG("playback: PCM %d running: state %d, %u period events, %lu periods "
	    "written, hw_ptr %lu", _card.playback_pcm, status.state,
	    _card.playback->periods, _card.playback_periods, status.hw_ptr);
}


/*
 * Capture: the PCM named by the config (PCM 8, "audio record") runs while
 * its 'capture' node is enabled. Every period read from it goes to the Play
 * sessions "mic_left" and "mic_right", whoever records from the mixer.
 *
 * The task looks for captured periods at every wake-up and wakes up once
 * per period while the PCM is open, so that the samples also flow if the
 * period events of the PCM do not arrive; their absence is reported once.
 */
static void capture_close(char const *why)
{
	struct handle *h = _card.capture;

	if (!h)
		return;

	_card.capture = NULL;

	handle_ioctl(h, SNDRV_PCM_IOCTL_DROP, NULL);
	handle_close(h);

	pixel_audio_play_stop();

	LOG("capture: PCM %d stopped and closed (%s), %lu periods read, %u "
	    "overruns", _card.capture_pcm, why, _card.capture_periods,
	    _card.capture_overruns);
}


static void capture_open(void)
{
	struct snd_pcm_runtime const *rt;
	int err = pixel_audio_capture_sessions();

	if (err) {
		_card.failed++;
		LOG("capture: FAILED: Play sessions \"mic_left\"/\"mic_right\" not "
		    "available (%d), PCM %d not started", err, _card.capture_pcm);
		return;
	}

	_card.capture = pcm_open_prepared(_card.capture_pcm, 1,
	                                  BUFFER_PERIODS*PERIOD_FRAMES, &err);
	if (!_card.capture) {
		_card.failed++;
		LOG("capture: FAILED: PCM %d not started: %d (the line above names "
		    "the step; -22 at prepare: no back end, i.e., no microphone "
		    "routed to the PCM)", _card.capture_pcm, err);
		return;
	}

	/*
	 * The prepare has sent the record parameters, the microphone list and
	 * the start of the microphone input: what the firmware said about them
	 * is printed before the stream starts (nothing waits for samples yet)
	 */
	if (!_card.playback)
		pixel_audio_aoc_log_drain(32, true);

	/* period events of this file wake the card task */
	if (_card.capture->file.f_op->fasync)
		_card.capture->file.f_op->fasync(-1, &_card.capture->file, 1);

	_card.capture_periods        = 0;
	_card.capture_overruns       = 0;
	_card.capture_event_jiffies  = jiffies;
	_card.capture_stall_reported = false;
	memset(_card.capture_peak, 0, sizeof(_card.capture_peak));
	memset(_card.capture_sum,  0, sizeof(_card.capture_sum));
	memset(_card.capture_dc,   0, sizeof(_card.capture_dc));

	err = handle_ioctl(_card.capture, SNDRV_PCM_IOCTL_START, NULL);
	if (err) {
		_card.failed++;
		LOG("capture: FAILED: start of PCM %d: %d", _card.capture_pcm, err);
		capture_close("start failed");
		return;
	}

	_card.capture_starts++;

	rt = pcm_runtime(_card.capture);
	if (rt)
		LOG("capture: PCM %d started (start %u): %u channels, %u Hz, %s "
		    "(%d bit), period %lu frames, buffer %lu frames; read per %u "
		    "frames into the Play sessions \"mic_left\"/\"mic_right\"",
		    _card.capture_pcm, _card.capture_starts, rt->channels, rt->rate,
		    rt->format == SNDRV_PCM_FORMAT_S16_LE ? "S16_LE" : "format?",
		    snd_pcm_format_width(rt->format), rt->period_size,
		    rt->buffer_size, PERIOD_FRAMES);
	else
		LOG("capture: PCM %d started (start %u)", _card.capture_pcm,
		    _card.capture_starts);
}


/*
 * Level lines for the first seconds after a start: the highest magnitude,
 * the mean magnitude and the mean value ("dc") of each channel, per 100 ms
 * of samples during the first two seconds and per second afterwards. A
 * microphone that works shows values that follow the sound in front of it;
 * all zero means that the AoC delivers silence, a "dc" as large as the
 * mean magnitude is a constant level.
 */
static void capture_level(short const *frames)
{
	unsigned long const fine_end = CAPTURE_FINE_SECONDS*PERIODS_PER_SECOND;

	unsigned long periods;
	unsigned      i, c, interval;
	bool          fine;

	for (i = 0; i < PERIOD_FRAMES*PIXEL_AUDIO_CHANNELS; i++) {
		int const v = frames[i] < 0 ? -(int)frames[i] : frames[i];

		c = i % PIXEL_AUDIO_CHANNELS;
		_card.capture_sum[c] += v;
		_card.capture_dc[c]  += frames[i];
		if (v > _card.capture_peak[c])
			_card.capture_peak[c] = v;
	}

	periods  = ++_card.capture_periods;
	fine     = periods <= fine_end;
	interval = fine ? CAPTURE_FINE_PERIODS : PERIODS_PER_SECOND;

	if (periods % interval)
		return;

	if (periods <= CAPTURE_LEVEL_SECONDS*PERIODS_PER_SECOND) {

		unsigned const frames_n = interval*PERIOD_FRAMES;
		char when[32];

		if (fine)
			scnprintf(when, sizeof(when), "%lu ms",
			          periods*1000/PERIODS_PER_SECOND);
		else
			scnprintf(when, sizeof(when), "second %lu",
			          periods/PERIODS_PER_SECOND);

		LOG("capture: %s: left peak %d mean %u dc %d, right peak %d mean "
		    "%u dc %d (magnitudes, full scale 32768), %u period events, %u "
		    "overruns", when,
		    _card.capture_peak[0],
		    (unsigned)div_u64(_card.capture_sum[0], frames_n),
		    (int)div_s64(_card.capture_dc[0], frames_n),
		    _card.capture_peak[1],
		    (unsigned)div_u64(_card.capture_sum[1], frames_n),
		    (int)div_s64(_card.capture_dc[1], frames_n),
		    _card.capture ? _card.capture->periods : 0,
		    _card.capture_overruns);
	}

	memset(_card.capture_peak, 0, sizeof(_card.capture_peak));
	memset(_card.capture_sum,  0, sizeof(_card.capture_sum));
	memset(_card.capture_dc,   0, sizeof(_card.capture_dc));
}


/* the stream is in XRUN: prepare and start it again */
static void capture_recover(char const *where)
{
	int err;

	if (++_card.capture_overruns <= 5 || _card.capture_overruns % 100 == 0)
		LOG("capture: overrun %u (%s), restarting", _card.capture_overruns,
		    where);

	err = handle_ioctl(_card.capture, SNDRV_PCM_IOCTL_PREPARE, NULL);
	if (!err)
		err = handle_ioctl(_card.capture, SNDRV_PCM_IOCTL_START, NULL);

	/* the samples that follow are not adjacent to the ones played last */
	pixel_audio_play_stop();

	if (err) {
		LOG("capture: FAILED: restart of PCM %d after an overrun: %d",
		    _card.capture_pcm, err);
		capture_close("restart failed");
	}
}


static void capture_service(void)
{
	static short frames[PERIOD_FRAMES*PIXEL_AUDIO_CHANNELS];

	struct snd_pcm_status64 status;
	unsigned n = 0;

	while (_card.capture && n++ < BUFFER_PERIODS) {
		struct snd_xferi xfer = { };
		int err;

		err = pcm_status(_card.capture, &status);
		if (err) {
			LOG("capture: FAILED: status of PCM %d: %d", _card.capture_pcm,
			    err);
			capture_close("status error");
			return;
		}

		if (status.state == SNDRV_PCM_STATE_XRUN) {
			capture_recover("status");
			return;
		}

		if (status.state != SNDRV_PCM_STATE_RUNNING
		 || status.avail < PERIOD_FRAMES)
			return;

		xfer.buf    = frames;
		xfer.frames = PERIOD_FRAMES;

		err = handle_ioctl(_card.capture, SNDRV_PCM_IOCTL_READI_FRAMES, &xfer);
		if (err == -EAGAIN)
			return;

		/* an overrun the status ioctl detected itself, see playback */
		if (err == -EPIPE) {
			capture_recover("read");
			return;
		}

		if (err) {
			/* not per period: stop until the next config */
			LOG("capture: FAILED: read from PCM %d: %d", _card.capture_pcm,
			    err);
			capture_close("read error");
			return;
		}

		capture_level(frames);
		pixel_audio_play_period(frames);
	}
}


static void capture_check_stall(void)
{
	struct snd_pcm_status64 status;

	if (!_card.capture || _card.capture_stall_reported
	 || time_before(jiffies, _card.capture_event_jiffies
	                         + msecs_to_jiffies(STALL_MS)))
		return;

	_card.capture_stall_reported = true;

	if (pcm_status(_card.capture, &status))
		return;

	LOG("capture: WARNING: PCM %d got no period event for %u ms: state %d "
	    "(2 prepared, 3 running, 4 xrun), hw_ptr %lu, %lu frames available, "
	    "%lu periods read, %u period events", _card.capture_pcm, STALL_MS,
	    status.state, status.hw_ptr, status.avail, _card.capture_periods,
	    _card.capture->periods);
}


/***************************
 ** Applying a new config **
 ***************************/

static bool plan_names_control(struct pixel_audio_plan const *plan,
                               char const *name)
{
	unsigned i;

	for (i = 0; plan && i < plan->num_actions; i++)
		if (plan->actions[i].type == PIXEL_AUDIO_ACTION_CONTROL
		 && strcmp(plan->actions[i].write.name, name) == 0)
			return true;
	return false;
}


/* write the 'default' nodes of 'name', or say that there are none */
static void revert_control(struct pixel_audio_plan const *plan, char const *name)
{
	unsigned i;

	for (i = 0; i < plan->num_defaults; i++) {
		if (strcmp(plan->defaults[i].name, name) == 0) {
			apply_group(&plan->defaults[i], plan->num_defaults - i, true,
			            true, "revert");
			return;
		}
	}
	LOG("WARNING: revert: no default value for \"%s\", left as it is", name);
}


/*
 * Revert the controls the old plan wrote and the new one does not name, in
 * reverse order of their application
 */
static void revert_disappeared(struct pixel_audio_plan const *old,
                               struct pixel_audio_plan const *new)
{
	int i, k;

	if (!old)
		return;

	for (i = (int)old->num_actions - 1; i >= 0; i--) {
		struct pixel_audio_action const *a = &old->actions[i];
		bool later = false;

		if (a->type != PIXEL_AUDIO_ACTION_CONTROL
		 || plan_names_control(new, a->write.name))
			continue;

		/* each control once: at its last appearance in the old plan */
		for (k = i + 1; k < (int)old->num_actions && !later; k++)
			later = old->actions[k].type == PIXEL_AUDIO_ACTION_CONTROL
			     && strcmp(old->actions[k].write.name, a->write.name) == 0;

		if (!later)
			revert_control(new, a->write.name);
	}
}


static void apply_tuning(struct pixel_audio_action const *a,
                         struct tuning *sent, unsigned *num_sent)
{
	struct tuning const t = { a->block, a->component, a->key, a->value };
	unsigned i;
	bool known = false;

	/* a parameter the previous config set to the same value is not resent */
	for (i = 0; i < _card.num_tunings && !known; i++)
		known = memcmp(&_card.tunings[i], &t, sizeof(t)) == 0;

	if (!known && tuning_send(&t)) {
		_card.failed++;
		return;   /* not remembered: the next config tries again */
	}

	if (*num_sent < MAX_TUNINGS)
		sent[(*num_sent)++] = t;
}


/* call volume step a plan asks for (tuning 19/3/18), 4 if it names none */
static unsigned plan_call_volume(struct pixel_audio_plan const *plan)
{
	unsigned i;

	for (i = 0; i < plan->num_actions; i++) {
		struct pixel_audio_action const *a = &plan->actions[i];

		if (a->type == PIXEL_AUDIO_ACTION_TUNING
		 && a->block == VOICE_BLOCK && a->component == VOICE_COMPONENT
		 && a->key == VOICE_KEY_VOLUME)
			return a->value;
	}
	return 4;
}


/* a call route that was not the route of the config before */
static bool plan_enters_call_route(struct pixel_audio_plan const *plan,
                                   struct pixel_audio_plan const *old)
{
	return (strcmp(plan->route, "handset") == 0 || strcmp(plan->route, "speaker") == 0)
	    && (!old || strcmp(old->route, plan->route) != 0);
}


static void apply_config(void)
{
	struct pixel_audio_plan const *plan = pixel_audio_plan_acquire();
	struct pixel_audio_plan const *old  = _card.plan;
	struct tuning sent[MAX_TUNINGS] = { };
	unsigned num_sent = 0, i;
	bool voice_tuned = false;

	if (!plan) {
		LOG("WARNING: config not applied: out of memory");
		return;
	}

	_card.writes = _card.unchanged = _card.failed = _card.unknown = 0;

	/* 1. session-backed PCMs that are disabled or moved */
	if (_card.playback && plan->playback_pcm != _card.playback_pcm)
		playback_close("config");

	if (_card.capture && plan->capture_pcm != _card.capture_pcm)
		capture_close("config");

	/* 2. streams the new config does not hold, before their controls */
	streams_release(plan);

	/* 3. controls that are no longer set go back to their default */
	revert_disappeared(old, plan);

	/* 4. once: the initial values of all controls */
	if (!_card.baseline_done) {
		for (i = 0; i < plan->num_defaults; )
			i += apply_group(&plan->defaults[i], plan->num_defaults - i,
			                 true, true, "default");
		if (plan->num_defaults)
			_card.baseline_done = true;
	}

	/* 5. the new list in order */
	for (i = 0; i < plan->num_actions; ) {
		struct pixel_audio_action const *a = &plan->actions[i];

		switch (a->type) {

		case PIXEL_AUDIO_ACTION_CONTROL: {
			/* adjacent control nodes of one control form one write */
			struct pixel_audio_write *group;
			unsigned n = 1, k;

			while (i + n < plan->num_actions
			    && plan->actions[i + n].type == PIXEL_AUDIO_ACTION_CONTROL
			    && strcmp(plan->actions[i + n].write.name, a->write.name) == 0)
				n++;

			group = kmalloc_array(n, sizeof(*group), GFP_KERNEL);
			if (group) {
				for (k = 0; k < n; k++)
					group[k] = plan->actions[i + k].write;

				/*
				 * A control the previous config set already is written
				 * only if its value changes. One that is newly named is
				 * always written: the HAL writes e.g. the in-call mutes at
				 * every call start, whatever their value was.
				 */
				apply_group(group, n, false,
				            plan_names_control(old, a->write.name), "control");
				kfree(group);

			} else {
				_card.failed++;
			}
			i += n;
			break;
		}

		case PIXEL_AUDIO_ACTION_STREAM:
			stream_hold(a->pcm, a->capture);

			/*
			 * The voice processor's configuration follows the start of
			 * the call's downlink at once, as the HAL sends it. Sent at
			 * the end of the config, the first two seconds of a call
			 * were muted; sent before the start (with the telephony
			 * mode), the firmware took it, but returned to "Mixer 0
			 * active (fortemedia - full)" when the call path started.
			 */
			if (!voice_tuned && a->pcm == VOICE_RX_PCM && !a->capture
			 && plan_enters_call_route(plan, old)) {
				voice_tuning_upload(plan->route, plan_call_volume(plan));
				voice_tuned = true;
			}
			i++;
			break;

		case PIXEL_AUDIO_ACTION_TUNING:
			apply_tuning(a, sent, &num_sent);
			i++;
			break;
		}
	}

	memcpy(_card.tunings, sent, sizeof(sent));
	_card.num_tunings = num_sent;

	/* 6. session-backed PCMs of the new config */
	_card.playback_pcm = plan->playback_pcm;
	_card.capture_pcm  = plan->capture_pcm;

	/* a new config may cure a failed start */
	_card.playback_error = 0;

	if (_card.capture_pcm >= 0 && !_card.capture)
		capture_open();

	/*
	 * A call route that was not the route before: the voice processor
	 * needs its configuration for this output device. Normally sent when
	 * the call's downlink PCM is opened (above); here for a config that does
	 * not open it, e.g. the switch between earpiece and loudspeaker within
	 * a call.
	 */
	if (!voice_tuned && plan_enters_call_route(plan, old))
		voice_tuning_upload(plan->route, plan_call_volume(plan));

	_card.plan = plan;
	_card.applied++;

	LOG("config %u applied: route \"%s\", %u control writes, %u unchanged, "
	    "%u failed, %u unknown controls, %u tuning parameters, playback PCM "
	    "%d, capture PCM %d", _card.applied, plan->route, _card.writes,
	    _card.unchanged, _card.failed, _card.unknown, num_sent,
	    _card.playback_pcm, _card.capture_pcm);

	pixel_audio_plan_release(old);

	mixer_check_new_controls();
}


/***************
 ** Card task **
 ***************/

static int card_task(void *arg)
{
	_card.task = current;

	for (;;) {

		unsigned long timeout = msecs_to_jiffies(STALL_MS);

		if (test_and_clear_bit(PIXEL_AUDIO_EVENT_CONFIG, &_card.events))
			apply_config();

		if (test_and_clear_bit(PIXEL_AUDIO_EVENT_WAKEUP, &_card.events))
			_card.playback_armed = true;

		if (test_and_clear_bit(PIXEL_AUDIO_EVENT_PLAYBACK, &_card.events)) {
			_card.playback_event_jiffies  = jiffies;
			_card.playback_stall_reported = false;
			playback_report_running();
		}

		playback_service();
		playback_check_stall();

		if (test_and_clear_bit(PIXEL_AUDIO_EVENT_CAPTURE, &_card.events)) {
			_card.capture_event_jiffies  = jiffies;
			_card.capture_stall_reported = false;
		}

		capture_service();
		capture_check_stall();

		mixer_check_new_controls();

		if (READ_ONCE(_card.events))
			continue;

		/*
		 * Sleep until the next event. With a PCM running, wake up once per
		 * second to notice a stream that stopped advancing.
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		if (READ_ONCE(_card.events)) {
			__set_current_state(TASK_RUNNING);
			continue;
		}

		if (_card.playback_probing || _card.capture)
			timeout = max(1UL, msecs_to_jiffies(10));     /* one period */
		else if (_card.playback_pcm >= 0 && !_card.playback
		      && _card.playback_armed && _card.playback_error)
			timeout = time_after(_card.playback_retry_at, jiffies)
			        ? _card.playback_retry_at - jiffies : 1;
		else if (!_card.playback && !_card.capture)
			timeout = msecs_to_jiffies(5000);

		schedule_timeout(timeout);

		/*
		 * The Genode side wakes the task with 'lx_emul_task_unblock', which
		 * leaves the Linux task state as it was
		 */
		__set_current_state(TASK_RUNNING);
	}
	return 0;
}


/************
 ** Stages **
 ************/

static unsigned count_pcms(void)
{
	struct snd_device *dev;
	unsigned n = 0;

	list_for_each_entry(dev, &_card.snd->devices, list)
		if (dev->type == SNDRV_DEV_PCM
		 && !((struct snd_pcm *)dev->device_data)->internal)
			n++;
	return n;
}


/**
 * Stage "card": the amplifiers, then the sound card
 *
 * The services of the AoC appear as devices when the AoC comes online;
 * aoc_alsa binds the 32 audio services ("alsa-aoc communication is
 * ready!"). Only then the card's probe can succeed, so its device is created
 * here and not at boot.
 */
int pixel_audio_stage_card(void)
{
	struct platform_device *sound;
	unsigned waited, i;
	int amps = 0;

	/*
	 * An amplifier that fails its probe leaves a "nop" codec behind, the
	 * card registers without it. Only a missing SPI controller is fatal
	 * for the card: no codec component exists then.
	 */
	amps = pixel_audio_stage_amp();

	pixel_audio_set_step("card: waiting for aoc_alsa to bind the audio "
	                     "services");

	for (waited = 0; waited < 5000 && !aoc_audio_current_state(); waited += 20)
		msleep(20);

	if (!aoc_audio_current_state()) {
		LOG("stage card FAILED: aoc_alsa bound %d audio services after "
		    "%u ms, not all (\"alsa-aoc communication is ready!\" missing)",
		    aoc_audio_service_num(), waited);
		return -ENODEV;
	}

	pixel_audio_set_step("card: probe of " PIXEL_AUDIO_DT_SOUND);

	sound = pixel_audio_create_device(PIXEL_AUDIO_DT_SOUND);

	/* a deferred probe is retried by the driver core */
	for (waited = 0; sound && !sound->dev.driver && waited < 3000; waited += 20)
		msleep(20);

	for (i = 0; i < SNDRV_CARDS && !_card.snd; i++)
		_card.snd = snd_card_ref(i);

	if (!sound || !sound->dev.driver || !_card.snd) {
		LOG("stage card FAILED: probe of %s (device %s, driver %s, sound "
		    "card %s): a missing platform, CPU or codec component defers "
		    "the registration, see the messages above%s",
		    PIXEL_AUDIO_DT_SOUND, sound ? "created" : "not created",
		    sound && sound->dev.driver ? "bound" : "not bound",
		    _card.snd ? "registered" : "not registered",
		    amps < 0 ? "; SPI7 did not probe, so the amplifier codecs do "
		               "not exist" : "");
		return -ENODEV;
	}

	LOG("stage card ok: sound card \"%s\" registered: %u PCM devices, %d "
	    "controls, %d AoC audio services", _card.snd->longname, count_pcms(),
	    _card.snd->controls_count, aoc_audio_service_num());
	return 0;
}


/**
 * Stage "mixer": list all controls, read each once, generate the report
 */
int pixel_audio_stage_mixer(void)
{
	unsigned i, by_type[SNDRV_CTL_ELEM_TYPE_LAST + 1] = { };
	int err = 0;

	pixel_audio_set_step("mixer: enumerating and reading the controls (a "
	                     "read of an AoC control sends an AoC command)");

	_card.ctl_handle = handle_open(_card.snd->ctl_dev.devt, &err);
	if (!_card.ctl_handle) {
		LOG("stage mixer FAILED: control device of the card not opened: %d",
		    err);
		return err ? err : -ENODEV;
	}

	/*
	 * The firmware logs every command of the control reads. First let
	 * the log thread print what is left of the firmware's boot log, then
	 * keep the routine lines of the reads out of the log.
	 */
	pixel_audio_aoc_log_drain(400, false);
	pixel_audio_aoc_log_quiet(true);

	err = mixer_refresh();

	pixel_audio_aoc_log_drain(64, true);
	pixel_audio_aoc_log_quiet(false);

	if (err < 0) {
		LOG("stage mixer FAILED: listing the controls: %d", err);
		return err;
	}

	for (i = 0; i < _card.num_ctls; i++)
		if ((unsigned)_card.ctls[i]->info.type <= SNDRV_CTL_ELEM_TYPE_LAST)
			by_type[_card.ctls[i]->info.type]++;

	mixer_report();

	LOG("stage mixer ok: %u controls (%u bool, %u int, %u enum, %u bytes, "
	    "%u other), %u read, %u read errors, %u not read; report \"mixer\" "
	    "generated", _card.num_ctls,
	    by_type[SNDRV_CTL_ELEM_TYPE_BOOLEAN],
	    by_type[SNDRV_CTL_ELEM_TYPE_INTEGER],
	    by_type[SNDRV_CTL_ELEM_TYPE_ENUMERATED],
	    by_type[SNDRV_CTL_ELEM_TYPE_BYTES],
	    _card.num_ctls - by_type[SNDRV_CTL_ELEM_TYPE_BOOLEAN]
	                   - by_type[SNDRV_CTL_ELEM_TYPE_INTEGER]
	                   - by_type[SNDRV_CTL_ELEM_TYPE_ENUMERATED]
	                   - by_type[SNDRV_CTL_ELEM_TYPE_BYTES],
	    _card.read_ok, _card.read_failed, _card.read_skipped);
	return 0;
}


/**
 * Stage "play": open the Record sessions and start the card task
 *
 * The stage does not wait for the card task, which applies every config
 * (controls, streams, tuning), and feeds the playback PCM once the Record
 * sessions deliver samples. It logs when the PCM runs.
 */
int pixel_audio_stage_play(void)
{
	int err;

	pixel_audio_set_step("play: Record sessions");

	err = pixel_audio_playback_sessions();
	if (err) {
		LOG("stage play FAILED: Record sessions \"left\"/\"right\" not "
		    "available: %d", err);
		return err;
	}

	pixel_audio_set_step("play: starting the card task");

	set_bit(PIXEL_AUDIO_EVENT_CONFIG, &_card.events);
	set_bit(PIXEL_AUDIO_EVENT_WAKEUP, &_card.events);

	if (IS_ERR(kthread_run(card_task, NULL, "pixel_audio_card"))) {
		LOG("stage play FAILED: card task not started");
		return -ENOMEM;
	}

	LOG("stage play ok: Record sessions opened, card task started");
	return 0;
}
