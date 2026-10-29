/*
 * \brief  Forwarding of the AoC firmware's own log
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * The AoC firmware writes its log into the service "logging". On Android
 * the daemon 'aocd' opens the character device of that service
 * (/dev/acd-logging, aoc_char_dev.c: a plain 'aoc_service_read' per 'read')
 * once the AoC is online and prints what it reads with the log tag "AOC".
 * It sends nothing to the service and issues no ioctl: opening and reading
 * is all it takes, and the firmware delivers what it buffered since its
 * boot (banner of the four cores first).
 *
 * aoc_char_dev.c is not built. The same read is made here by a kernel
 * thread, each firmware line is printed as
 *
 *   pixel_audio: AOC: <line>
 *
 * Framing, as 'aocd' handles it (service "logging": a queue of 256
 * entries of 120 bytes): each read returns one entry. An entry is plain text, zero
 * terminated ("MSG: controller.cc, 275: ...", a banner line), or starts
 * with the byte 0x02, followed by a level character ('C' critical, 'E'
 * error, 'M' message), the length of the text as 16-bit little endian, and
 * the text. The core prefix of a line ("F1:") comes as an entry of its
 * own: an entry of up to four bytes is the beginning of the line that the
 * next entry completes (aocd keeps such an entry in its buffer). A ring
 * service is handled as a byte stream cut at line ends.
 *
 * The log goes over a 115200-baud line, a printed line blocks the whole
 * component for about 10 ms, the card task included. Hence:
 *
 * - The thread sleeps one tick after every line it printed, so that the
 *   card task runs in between (32 lines in a row make the playback PCM
 *   underrun).
 * - The output is bounded by a token bucket (settings 'aoc_log_rate' and
 *   'aoc_log_burst'). The thread never waits for the budget: it reads what
 *   the firmware has and drops what exceeds the budget, reporting the
 *   number of dropped lines.
 * - Replies to commands that succeeded ("... cmd ID: ..., rc: 0") are
 *   counted but not printed unless 'aoc_log_replies' is set: the firmware
 *   logs two lines per command and the line of the command says it all.
 * - Phases that are known to produce nothing of interest (the mixer stage
 *   reads 460 controls) are declared quiet by the caller: only lines of
 *   the error level are printed then. The budget is full again at the end
 *   of a quiet phase.
 *
 * The vendor code waits for the reply of an AoC command in a loop that
 * never sleeps, so the thread does not run while a stage issues one
 * command after the other. 'pixel_audio_aoc_log_drain' is the scheduling
 * point: the caller sleeps until the thread has printed what the firmware
 * logged so far, which also keeps the firmware lines next to the driver
 * lines they belong to.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <aoc.h>          /* google-modules/aoc, no include guard */
#include <aoc_ipc_core.h>

extern struct platform_device *aoc_platform_device;

#define LOG(fmt, ...) printk("pixel_audio: aoc_log: " fmt "\n", ##__VA_ARGS__)

enum {
	ENTRY_MAX       = 512,   /* the read size of aocd                       */
	STREAM_CHUNK    = 2048,  /* read size for a ring service                */
	LINE_MAX_CHARS  = 240,   /* longer lines are cut                        */
	WAIT_MS         = 100,   /* wait for the service's interrupt at most    */
	BATCH           = 32,    /* unprinted entries read before a yield       */
	PREFIX_MAX      = 4,     /* an entry up to this size starts a line      */
	STATUS_MS       = 10000, /* counters are logged at most this often      */
	FRAMED          = 0x02,  /* first byte of an entry with a header        */
};

static struct
{
	struct aoc_service_dev *dev;
	char const             *name;

	/* the line that is being collected */
	char     line[LINE_MAX_CHARS + 1];
	unsigned line_len;
	bool     line_cut;

	/* output budget in lines, scaled by HZ */
	long          tokens;
	unsigned long refilled;       /* jiffies of the last refill */

	/* only lines of the error level are printed (a caller's quiet phase) */
	bool quiet;
	bool stopped;

	/* counters */
	unsigned long entries, printed, replies, quieted, dropped, cut;
	unsigned long dropped_reported;
	unsigned long status_printed, status_at;
} _log;


static struct aoc_service_dev *service_by_name(char const *name)
{
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


/************
 ** Budget **
 ************/

static void budget_refill(void)
{
	unsigned long const now  = jiffies;
	long          const max  = (long)pixel_audio_config.aoc_log_burst * HZ;
	long          const rate = (long)pixel_audio_config.aoc_log_rate;

	_log.tokens += (long)(now - _log.refilled) * rate;
	if (_log.tokens > max)
		_log.tokens = max;

	_log.refilled = now;
}


/*
 * A line of the error or critical level may overdraw the budget by a
 * quarter of the burst, so that the reason of a failure is not lost behind
 * a flood of routine lines.
 */
static bool budget_take(bool important)
{
	long const reserve = important
	                   ? (long)pixel_audio_config.aoc_log_burst * HZ / 4 : 0;

	budget_refill();

	if (_log.tokens + reserve < HZ)
		return false;

	_log.tokens -= HZ;
	return true;
}


/************
 ** Output **
 ************/

static bool contains(char const *line, char const *pattern)
{
	return strstr(line, pattern) != NULL;
}


static bool ends_with(char const *line, unsigned len, char const *tail)
{
	unsigned const n = strlen(tail);

	return len >= n && memcmp(line + len - n, tail, n) == 0;
}


/* say how many lines the rate limit has cost since the last report */
static void report_dropped(void)
{
	if (_log.dropped == _log.dropped_reported)
		return;

	budget_refill();
	if (_log.tokens < HZ)
		return;

	LOG("%lu lines dropped (limit %u lines/s, burst %u)",
	    _log.dropped - _log.dropped_reported, pixel_audio_config.aoc_log_rate,
	    pixel_audio_config.aoc_log_burst);

	_log.dropped_reported = _log.dropped;
}


/*
 * The current line is complete, 'level' is the level character of a framed
 * entry or 0
 */
static void emit_line(char level)
{
	char * const line   = _log.line;
	unsigned     len    = _log.line_len;
	bool   const cut    = _log.line_cut;
	char const  *prefix = "";
	bool         important;

	_log.line_len = 0;
	_log.line_cut = false;

	/* the banner is framed by empty lines */
	while (len && (line[len - 1] == ' ' || line[len - 1] == '\t'))
		len--;
	if (!len)
		return;

	line[len] = 0;

	/* the level of a framed entry is in its header only */
	if (level == 'E' && !contains(line, "ERR"))
		prefix = "ERR: ";
	if (level == 'C' && !contains(line, "CRT"))
		prefix = "CRT: ";

	important = level == 'E' || level == 'C'
	         || contains(line, "ERR") || contains(line, "CRT");

	if (!important && !pixel_audio_config.aoc_log_replies
	 && contains(line, "cmd ID:") && ends_with(line, len, "rc: 0")) {
		_log.replies++;
		return;
	}

	if (!important && _log.quiet) {
		_log.quieted++;
		return;
	}

	if (!budget_take(important)) {
		_log.dropped++;
		return;
	}

	report_dropped();

	printk("pixel_audio: AOC: %s%s%s\n", prefix, line, cut ? " ..." : "");

	_log.printed++;
}


/* add text to the current line, emit the line at each line end */
static void feed(char const *text, unsigned len, char level)
{
	unsigned i;

	for (i = 0; i < len; i++) {
		char const c = text[i];

		if (c == '\n' || c == '\r') {
			emit_line(level);
			continue;
		}

		/* the terminator of an entry */
		if (c == 0)
			continue;

		if (_log.line_len == LINE_MAX_CHARS) {
			if (!_log.line_cut)
				_log.cut++;
			_log.line_cut = true;
			continue;
		}

		/* nothing but printable ASCII reaches the serial line */
		_log.line[_log.line_len++] = (c >= 0x20 && c < 0x7f) ? c : '.';
	}
}


static void handle_entry(u8 const *entry, unsigned len, bool stream)
{
	_log.entries++;

	if (stream) {
		feed((char const *)entry, len, 0);
		return;
	}

	if (len >= 4 && entry[0] == FRAMED) {
		unsigned const text_len = entry[2] | (entry[3] << 8);

		feed((char const *)entry + 4, min(text_len, len - 4), (char)entry[1]);
		emit_line((char)entry[1]);
		return;
	}

	/*
	 * Plain text: what follows the last line end is a line too, unless the
	 * entry is the short beginning of a line ("F1:")
	 */
	feed((char const *)entry, len, 0);
	if (len > PREFIX_MAX)
		emit_line(0);
}


static void status(bool force)
{
	if (!force && (_log.printed == _log.status_printed
	            || time_before(jiffies, _log.status_at)))
		return;

	_log.status_printed = _log.printed;
	_log.status_at      = jiffies + msecs_to_jiffies(STATUS_MS);

	LOG("%lu entries read, %lu lines printed, not printed: %lu replies "
	    "\"rc: 0\", %lu in quiet phases, %lu dropped by the rate limit; %lu "
	    "cut at %u characters", _log.entries, _log.printed, _log.replies,
	    _log.quieted, _log.dropped, _log.cut, LINE_MAX_CHARS);
}


/************
 ** Thread **
 ************/

static int log_thread(void *arg)
{
	struct aoc_service_dev * const dev    = _log.dev;
	bool                     const stream = aoc_service_is_ring(dev->service);

	/*
	 * An entry of a queue must fit ('aoc_service_read' refuses a smaller
	 * buffer), a ring is read in pieces of any size
	 */
	size_t const size = stream ? STREAM_CHUNK
	                           : max_t(size_t, ENTRY_MAX,
	                                   aoc_service_message_size(dev->service,
	                                                            AOC_UP));
	u8           *buf;
	int           last_err = 0;
	unsigned      batch    = 0;
	unsigned long printed;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		LOG("FAILED: no memory for a %zu-byte entry, the firmware log is "
		    "not forwarded", size);
		return 0;
	}

	for (;;) {
		/*
		 * The read of the character device: never blocks here, and tells
		 * the AoC that there is room again if the service was full.
		 */
		ssize_t const n = aoc_service_read(dev, buf, size, false);

		if (n == -EAGAIN) {
			report_dropped();
			status(false);
			batch = 0;

			/*
			 * Nothing to read: wait for the interrupt of the service as
			 * 'poll' on the character device does, WAIT_MS at most. An
			 * interrupt that does not arrive delays the lines, it does
			 * not lose them.
			 */
			aoc_service_set_read_blocked(dev);
			wait_event_interruptible_timeout(
				*aoc_service_get_read_queue(dev),
				dev->dead || !aoc_online_state(dev)
				          || aoc_service_can_read(dev),
				msecs_to_jiffies(WAIT_MS));

			if (dev->dead || !aoc_online_state(dev)) {
				LOG("stopped: the AoC went offline");
				break;
			}
			continue;
		}

		if (n == -ENODEV || n == -EBUSY) {
			LOG("stopped: the AoC is offline or the service is gone (%zd)",
			    n);
			break;
		}

		if (n < 0) {
			if ((int)n != last_err)
				LOG("read of service \"%s\" failed: %zd (reported once "
				    "per error)", _log.name, n);
			last_err = (int)n;
			msleep(WAIT_MS);
			batch = 0;
			continue;
		}

		last_err = 0;

		printed = _log.printed;

		if (n > 0)
			handle_entry(buf, (unsigned)n, stream);

		/*
		 * A printed line has blocked the component for the time it takes
		 * on the serial line: let the other tasks of the driver run before
		 * the next one. Entries that are not printed cost nothing.
		 */
		if (_log.printed != printed || ++batch >= BATCH) {
			batch = 0;
			schedule_timeout_uninterruptible(1);
		}
	}

	_log.stopped = true;

	status(true);
	kfree(buf);
	return 0;
}


void pixel_audio_aoc_log_start(void)
{
	struct aoc_service_dev *dev;
	aoc_service            *service;

	if (!pixel_audio_config.aoc_log) {
		LOG("off (settings aoc_log=\"no\"), the firmware log stays unread");
		return;
	}

	if (_log.dev)
		return;

	/* as aocd: "logging", else "debug" */
	_log.name = "logging";
	dev = service_by_name(_log.name);
	if (!dev) {
		_log.name = "debug";
		dev = service_by_name(_log.name);
	}

	if (!dev) {
		LOG("FAILED: the AoC has neither a service \"logging\" nor "
		    "\"debug\" among its %d services, the firmware log is not "
		    "forwarded", aoc_num_services());
		return;
	}

	service = dev->service;

	if (aoc_service_message_slots(service, AOC_UP) == 0) {
		LOG("FAILED: service \"%s\" has no slot in the direction AoC -> AP, "
		    "nothing to read", _log.name);
		return;
	}

	get_device(&dev->dev);

	_log.dev      = dev;
	_log.tokens   = (long)pixel_audio_config.aoc_log_burst * HZ;
	_log.refilled = jiffies;

	_log.status_at = jiffies + msecs_to_jiffies(STATUS_MS);

	/* for a ring "waiting" is 1 if there are bytes to read */
	LOG("service \"%s\" (index %d, %s, interrupt %d): %zu slots of %zu "
	    "bytes AoC -> AP, %zu waiting; its lines follow with the prefix "
	    "\"AOC:\", at most %u lines/s after a burst of %u, replies "
	    "\"rc: 0\" %s", _log.name, dev->service_index,
	    aoc_service_is_ring(service)  ? "ring"  :
	    aoc_service_is_queue(service) ? "queue" : "buffer",
	    dev->mbox_index,
	    aoc_service_message_slots(service, AOC_UP),
	    aoc_service_message_size(service, AOC_UP),
	    aoc_service_slots_available_to_read(service, AOC_UP),
	    pixel_audio_config.aoc_log_rate, pixel_audio_config.aoc_log_burst,
	    pixel_audio_config.aoc_log_replies ? "printed" : "counted only");

	if (IS_ERR(kthread_run(log_thread, NULL, "pixel_audio_aoc_log"))) {
		LOG("FAILED: thread not started, the firmware log is not forwarded");
		put_device(&dev->dev);
		_log.dev = NULL;
	}
}


void pixel_audio_aoc_log_drain(unsigned max_lines, bool settle)
{
	unsigned long const printed = _log.printed;
	unsigned            ticks   = 0;

	if (!_log.dev || _log.stopped)
		return;

	/* the firmware logs a command a moment after it replied to it */
	if (settle)
		schedule_timeout_uninterruptible(1);

	/*
	 * The thread sleeps a tick after each printed line and reads on
	 * otherwise: every tick slept here is one line at most
	 */
	while (!_log.stopped && aoc_online_state(_log.dev)
	    && aoc_service_can_read(_log.dev)
	    && _log.printed - printed < max_lines && ticks++ < 4*max_lines + 16)
		schedule_timeout_uninterruptible(1);
}


void pixel_audio_aoc_log_quiet(bool quiet)
{
	if (!_log.dev || _log.quiet == quiet)
		return;

	_log.quiet = quiet;

	if (quiet)
		return;

	/*
	 * What follows a quiet phase is what the log is for (the commands of
	 * a config, a stream start): it gets a whole burst, whatever the
	 * firmware's boot log and the errors of the quiet phase have used up
	 */
	_log.tokens   = (long)pixel_audio_config.aoc_log_burst * HZ;
	_log.refilled = jiffies;

	LOG("end of the quiet phase: %lu lines not printed in quiet phases so "
	    "far (lines of the error level are always printed)", _log.quieted);
}
