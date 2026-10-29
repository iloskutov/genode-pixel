/*
 * \brief  Bring-up of the AoC DSP and of the sound card
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * The drivers (Trusty, GSA, AoC mailboxes, AoC core, SPI7, amplifiers,
 * aoc_alsa) are registered by the initcalls, their devices are created
 * here, one stage at a time, and each stage leaves exactly one result line
 * in the log:
 *
 *   pixel_audio: stage <name> ok: ...
 *   pixel_audio: stage <name> FAILED: ... <raw return values>
 *
 * On Android the firmware load is triggered by the 'aocd' daemon
 * through sysfs; here the driver triggers it itself (start_firmware_load).
 * The stages of the sound card (card, mixer, play) are in card.c, the
 * amplifier probe in amp.c.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>
#include <lx_user/init.h>

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/sched/task.h>
#include <linux/workqueue.h>

#include <linux/trusty/arm_ffa.h>
#include <linux/trusty/smcall.h>
#include <linux/trusty/sm_err.h>
#include <linux/trusty/trusty.h>
#include <linux/trusty/trusty_ipc.h>
#include <linux/gsa/gsa_aoc.h>
#include <linux/gsa/gsa_sjtag.h>

/* private headers of the vendor drivers */
#include <trusty-smc.h>   /* drivers/trusty */
#include <gsa_mbox.h>     /* drivers/soc/google/gsa */
#include <gsa_priv.h>
#include <aoc.h>          /* google-modules/aoc */

extern enum AOC_FW_STATE       aoc_state;
extern struct platform_device *aoc_platform_device;

#define LOG(fmt, ...) printk("pixel_audio: " fmt "\n", ##__VA_ARGS__)

enum { TIPC_ONLINE_TIMEOUT_MS = 10000, POLL_MS = 20, WATCH_PERIOD_MS = 5000 };


/***********
 ** State **
 ***********/

static struct platform_device *_gsa;
static struct platform_device *_trusty;
static struct platform_device *_aoc;

/* what is in progress, reported by the watcher if it does not advance */
static char const * volatile _step = "initcalls";
static bool volatile         _finished;

/* results of the GSA calls made by aoc.c during the firmware load */
static bool _load_called;
static int  _load_rc;
static bool _start_called;
static int  _start_rc;


char const *pixel_audio_current_step(void) { return _step; }
void pixel_audio_set_step(char const *step) { _step = step; }


static bool bound(struct platform_device const *pdev)
{
	return pdev && pdev->dev.driver;
}


/*************
 ** Watcher **
 *************/

/*
 * Several waits of the vendor code have no timeout (the GSA mailbox waits
 * for its response interrupt forever, standard calls to Trusty retry on
 * SM_ERR_BUSY forever). A step that hangs would leave a silent log, so a
 * second thread reports a step that does not advance.
 */
static int watch_thread(void *arg)
{
	char const *last  = NULL;
	unsigned    stuck = 0;

	while (!_finished) {

		msleep(WATCH_PERIOD_MS);

		if (_finished)
			break;

		if (_step != last) {
			last  = _step;
			stuck = 0;
			continue;
		}

		stuck += WATCH_PERIOD_MS;

		LOG("still in step \"%s\" for at least %u ms (Trusty NOP polls so "
		    "far: %lu)", last, stuck, pixel_audio_trusty_poll_count());
	}
	return 0;
}


/******************************************
 ** GSA calls of aoc.c during the load   **
 ******************************************/

/*
 * 'gsa_load_aoc_fw_image' of aoc.c (aoc_shim.h): GSA mailbox command 90,
 * header DMA address + physical address of the image body.
 *
 * GSA reads the image through its S2MPU, which the boot loader leaves
 * without access. Under LineageOS, pKVM programs it around this command,
 * inside a GSA wakelock (commands 150/151) that keeps the S2MPU powered.
 * The driver is built without CONFIG_GSA_PKVM, so the same is done here:
 * acquire, S2MPU, command 90, recorded faults, release.
 */
int pixel_audio_gsa_load_aoc_fw_image(struct device *gsa, dma_addr_t img_meta,
                                      phys_addr_t img_body)
{
	int rc, lock;

	_step = "load: GSA mailbox command 150 (WAKELOCK_ACQUIRE)";

	lock = gsa_send_simple_cmd(gsa, GSA_MB_CMD_WAKELOCK_ACQUIRE);
	if (lock) {
		LOG("load: GSA wakelock not acquired (%d), the S2MPU stays closed",
		    lock);
	} else {
		_step = "load: programming of the GSA S2MPU";
		rc = pixel_audio_s2mpu_open();
		if (rc)
			LOG("load: programming of the GSA S2MPU failed (%d)", rc);
	}

	_step = "load: GSA mailbox command 90 (LOAD_AOC_FW_IMG), "
	        "no timeout in gsa_mbox.c";

	rc = gsa_load_aoc_fw_image(gsa, img_meta, img_body);

	_load_called = true;
	_load_rc     = rc;

	if (!lock) {
		pixel_audio_s2mpu_faults("at command 90");
		gsa_send_simple_cmd(gsa, GSA_MB_CMD_WAKELOCK_RELEASE);
	}

	_step = "load: firmware parameters";
	return rc;
}


/*
 * 'gsa_send_aoc_cmd' of aoc.c (aoc_shim.h): a TIPC message to the hardware
 * manager "com.android.trusty.gsa.hwmgr.aoc". The result of the start command
 * is checked by stage "start".
 */
int pixel_audio_gsa_send_aoc_cmd(struct device *gsa, enum gsa_aoc_cmd cmd)
{
	int rc;

	if (cmd != GSA_AOC_START) {
		rc = gsa_send_aoc_cmd(gsa, cmd);
		LOG("GSA AoC command %d over TIPC returned %d", cmd, rc);
		return rc;
	}

	_start_called = true;

	_step = "start: GSA_AOC_START over TIPC";
	rc = gsa_send_aoc_cmd(gsa, GSA_AOC_START);

	_start_rc = rc;
	_step     = "start: waiting for the AoC to come online";
	return rc;
}


/****************
 ** Stage: smc **
 ****************/

/*
 * Fast calls only, as trusty.c issues them at the start of its probe:
 * version string and API version. FFA_VERSION and the list of interrupts
 * Trusty wants forwarded are read in addition (informational, no state is
 * changed by them).
 */
static int stage_smc(void)
{
	char     version[160];
	s32      len, api;
	unsigned i;

	_step = "smc: SMC_FC_GET_VERSION_STR";

	len = (s32)trusty_smc8(SMC_FC_GET_VERSION_STR, (u32)-1, 0, 0, 0, 0, 0, 0).r0;
	if (len <= 0) {
		LOG("stage smc FAILED: SMC_FC_GET_VERSION_STR(-1) returned %d (%#x)",
		    len, (u32)len);
		return -EIO;
	}

	for (i = 0; i < len && i < sizeof(version) - 1; i++) {
		s32 const c = (s32)trusty_smc8(SMC_FC_GET_VERSION_STR, i,
		                               0, 0, 0, 0, 0, 0).r0;
		if (c < 0) {
			LOG("stage smc FAILED: SMC_FC_GET_VERSION_STR(%u) returned %d",
			    i, c);
			return -EIO;
		}
		version[i] = (c == '\n') ? ' ' : (char)c;
	}
	version[i] = 0;

	_step = "smc: SMC_FC_API_VERSION";

	api = (s32)trusty_smc8(SMC_FC_API_VERSION, TRUSTY_API_VERSION_CURRENT,
	                       0, 0, 0, 0, 0, 0).r0;
	if ((u32)api == SM_ERR_UNDEFINED_SMC || api < 0
	 || api > TRUSTY_API_VERSION_CURRENT) {
		LOG("stage smc FAILED: SMC_FC_API_VERSION returned %d (%#x), "
		    "version string \"%s\"", api, (u32)api, version);
		return -EIO;
	}

	LOG("stage smc ok: trusty version \"%s\" (%d characters), "
	    "api version %d", version, len, api);

	return 0;
}


/****************
 ** Stage: gsa **
 ****************/

/*
 * GSA device probe (mailbox mapping and interrupt), then one read-only
 * mailbox command without data transfer: 100 = GSA_MB_CMD_SJTAG_GET_STATUS,
 * the command LineageOS issues at boot ("SJTAG_GET_STATUS: Success").
 */
static int stage_gsa(void)
{
	u32 rsp[3] = { 0, 0, 0 };
	int rc;

	_step = "gsa: probe of " PIXEL_AUDIO_DT_GSA;

	_gsa = pixel_audio_create_device(PIXEL_AUDIO_DT_GSA);
	if (!bound(_gsa)) {
		LOG("stage gsa FAILED: probe of %s (device %s, driver %s)",
		    PIXEL_AUDIO_DT_GSA, _gsa ? "created" : "not created",
		    bound(_gsa) ? "bound" : "not bound");
		return -ENODEV;
	}

	_step = "gsa: mailbox command 100 (SJTAG_GET_STATUS), "
	        "no timeout in gsa_mbox.c";

	rc = gsa_send_cmd(&_gsa->dev, GSA_MB_CMD_SJTAG_GET_STATUS, NULL, 0,
	                  rsp, ARRAY_SIZE(rsp));
	if (rc < 0) {
		LOG("stage gsa FAILED: mailbox command %d returned %d",
		    GSA_MB_CMD_SJTAG_GET_STATUS, rc);
		return rc;
	}

	LOG("stage gsa ok: mailbox command %d returned %d words: "
	    "debug_allowed=%#x hw_status=%#x debug_time=%#x",
	    GSA_MB_CMD_SJTAG_GET_STATUS, rc, rsp[0], rsp[1], rsp[2]);

	return 0;
}


/*****************
 ** Stage: tipc **
 *****************/

static void tipc_probe_event(void *arg, int event) { }

static struct tipc_msg_buf *tipc_probe_msg(void *arg, struct tipc_msg_buf *mb)
{
	return mb;
}

static const struct tipc_chan_ops tipc_probe_ops = {
	.handle_event = tipc_probe_event,
	.handle_msg   = tipc_probe_msg,
};


/*
 * Trusty core probe (version, API version, FF-A buffers for API 5, NOP
 * worker), its virtio child (device descriptor, vrings, TIPC), start of the
 * NOP poller. TIPC is usable once Trusty sent GO_ONLINE: a channel can be
 * created only then. Finally one harmless request to the GSA hardware
 * manager: the AoC state.
 */
static int stage_tipc(void)
{
	struct tipc_chan *chan = ERR_PTR(-ENOENT);
	unsigned          waited;
	int               rc;

	_step = "tipc: probe of " PIXEL_AUDIO_DT_TRUSTY " (fast calls, FF-A, "
	        "virtio standard calls)";

	_trusty = pixel_audio_create_device(PIXEL_AUDIO_DT_TRUSTY);
	if (!bound(_trusty)) {
		LOG("stage tipc FAILED: probe of %s (device %s, driver %s)",
		    PIXEL_AUDIO_DT_TRUSTY, _trusty ? "created" : "not created",
		    bound(_trusty) ? "bound" : "not bound");
		return -ENODEV;
	}

	rc = pixel_audio_trusty_poll_start(&_trusty->dev);
	if (rc) {
		LOG("stage tipc FAILED: NOP poller thread: %d", rc);
		return rc;
	}

	_step = "tipc: waiting for TIPC GO_ONLINE";

	for (waited = 0; waited < TIPC_ONLINE_TIMEOUT_MS; waited += POLL_MS) {
		chan = tipc_create_channel(NULL, &tipc_probe_ops, NULL);
		if (!IS_ERR(chan))
			break;
		msleep(POLL_MS);
	}

	if (IS_ERR(chan)) {
		LOG("stage tipc FAILED: TIPC not online after %u ms "
		    "(tipc_create_channel: %ld, NOP polls: %lu)",
		    waited, PTR_ERR(chan), pixel_audio_trusty_poll_count());
		return PTR_ERR(chan);
	}
	tipc_chan_destroy(chan);

	_step = "tipc: GSA_AOC_GET_STATE over TIPC (connect 5 s, reply 10 s)";

	rc = gsa_send_aoc_cmd(&_gsa->dev, GSA_AOC_GET_STATE);
	if (rc < 0) {
		LOG("stage tipc FAILED: GSA_AOC_GET_STATE returned %d "
		    "(NOP polls: %lu)", rc, pixel_audio_trusty_poll_count());
		return rc;
	}

	LOG("stage tipc ok: TIPC online, GSA_AOC_GET_STATE=%d "
	    "(0 inactive, 1 loaded, 2 running)", rc);

	if (rc != GSA_AOC_STATE_INACTIVE)
		LOG("tipc: WARNING: the AoC is not inactive, a firmware load is "
		    "expected to be refused by GSA");
	return 0;
}


/***************************
 ** Stages: load and start **
 ***************************/

static int create_bound(char const *path)
{
	struct platform_device const * const pdev = pixel_audio_create_device(path);

	if (bound(pdev))
		return 0;

	LOG("stage load FAILED: probe of %s (device %s, driver %s)", path,
	    pdev ? "created" : "not created", bound(pdev) ? "bound" : "not bound");
	return -ENODEV;
}


/*
 * Probe of the three AoC mailboxes and of the AoC core, then the firmware
 * load: aoc_fw_callback of aoc.c copies the image to the AoC DRAM, has it
 * authenticated by GSA (mailbox command 90), writes the firmware parameters
 * and sends GSA_AOC_START. The two GSA calls pass through the wrappers
 * above.
 */
static int stage_load(void)
{
	struct aoc_prvdata *prvdata;
	int rc, state;

	/*
	 * The crash path of aoc.c must not do harm during bring-up: no restart
	 * (it would access the unmapped blk_aoc window) and no panic when
	 * 'aoc_req' is not acknowledged.
	 */
	_step = "load: module parameters";

	rc = pixel_audio_set_bool_param("aoc_disable_restart", true);
	if (!rc)
		rc = pixel_audio_set_bool_param("aoc_panic_on_req_timeout", false);
	if (rc) {
		LOG("stage load FAILED: module parameters of aoc.c not found (%d), "
		    "the AoC is not probed", rc);
		return rc;
	}

	_step = "load: probe of the AoC mailboxes";

	if (create_bound(PIXEL_AUDIO_DT_MBOX0)
	 || create_bound(PIXEL_AUDIO_DT_MBOX1)
	 || create_bound(PIXEL_AUDIO_DT_MBOX2))
		return -ENODEV;

	_step = "load: probe of " PIXEL_AUDIO_DT_AOC;

	_aoc = pixel_audio_create_device(PIXEL_AUDIO_DT_AOC);
	if (!bound(_aoc) || aoc_platform_device != _aoc) {
		LOG("stage load FAILED: probe of %s (device %s, driver %s)",
		    PIXEL_AUDIO_DT_AOC, _aoc ? "created" : "not created",
		    bound(_aoc) ? "bound" : "not bound");
		return -ENODEV;
	}

	prvdata = platform_get_drvdata(_aoc);

	_step = "load: request_firmware and image checks";

	rc = start_firmware_load(&_aoc->dev);
	if (rc) {
		LOG("stage load FAILED: start_firmware_load returned %d", rc);
		return rc;
	}

	/*
	 * The load runs as a work item (request_firmware_nowait). On success it
	 * ends about 2 s after the start command (aoc.c keeps the "AP" awake
	 * for the AoC boot).
	 */
	__flush_workqueue(system_wq);

	if (!_load_called) {
		LOG("stage load FAILED: the image did not reach the GSA "
		    "authentication (ROM \"%s\" missing, invalid, unsigned or not "
		    "matching the driver's interface; see the aoc messages above)",
		    prvdata->firmware_name);
		return -ENOENT;
	}

	if (_load_rc) {
		LOG("stage load FAILED: GSA mailbox command %d (LOAD_AOC_FW_IMG) "
		    "returned %d", GSA_MB_CMD_LOAD_AOC_FW_IMG, _load_rc);
		return _load_rc;
	}

	_step = "load: GSA_AOC_GET_STATE over TIPC";

	state = gsa_send_aoc_cmd(&_gsa->dev, GSA_AOC_GET_STATE);

	LOG("stage load ok: firmware %s authenticated by GSA (command %d "
	    "returned 0), GSA_AOC_GET_STATE=%d (0 inactive, 1 loaded, 2 running)",
	    prvdata->firmware_version ? prvdata->firmware_version : "?",
	    GSA_MB_CMD_LOAD_AOC_FW_IMG, state);
	return 0;
}


static int stage_start(void)
{
	struct aoc_prvdata *prvdata = platform_get_drvdata(_aoc);
	unsigned waited;
	int      state;

	if (!_start_called) {
		LOG("stage start FAILED: aoc.c did not send GSA_AOC_START");
		return -EIO;
	}

	if (_start_rc < 0) {
		LOG("stage start FAILED: GSA_AOC_START over TIPC returned %d",
		    _start_rc);
		return _start_rc;
	}

	_step = "start: waiting for the AoC to come online";

	for (waited = 0; waited < pixel_audio_config.online_timeout_ms
	              && aoc_state != AOC_STATE_ONLINE; waited += POLL_MS)
		msleep(POLL_MS);

	if (aoc_state == AOC_STATE_ONLINE) {
		LOG("stage start ok: AoC online, firmware %s, %d services",
		    prvdata->firmware_version ? prvdata->firmware_version : "?",
		    aoc_num_services());

		/* what aocd does next on Android: read the firmware's log */
		pixel_audio_aoc_log_start();
		return 0;
	}

	_step = "start: GSA_AOC_GET_STATE over TIPC";

	state = gsa_send_aoc_cmd(&_gsa->dev, GSA_AOC_GET_STATE);

	LOG("stage start FAILED: AoC not online %u ms after the firmware load "
	    "ended: aoc_state=%d (1 loaded, 2 starting, 3 online, 4 ssr), "
	    "control-block magic %s, GSA_AOC_START returned %d, "
	    "GSA_AOC_GET_STATE=%d", waited, aoc_state,
	    aoc_fw_ready() ? "present" : "absent", _start_rc, state);
	return -ETIMEDOUT;
}


/**************
 ** Bring-up **
 **************/

static int bringup_thread(void *arg)
{
	static struct {
		char const *name;
		int (*fn)(void);
	} const stages[] = {
		{ "smc",   stage_smc   },
		{ "gsa",   stage_gsa   },
		{ "tipc",  stage_tipc  },
		{ "load",  stage_load  },
		{ "start", stage_start },

		/* sound card (card.c): needs the AoC online */
		{ "card",  pixel_audio_stage_card  },
		{ "mixer", pixel_audio_stage_mixer },
		{ "play",  pixel_audio_stage_play  },
	};

	unsigned i;
	int      rc = 0;

	for (i = 0; i < ARRAY_SIZE(stages); i++) {

		rc = stages[i].fn();
		if (rc)
			break;
	}

	if (rc)
		LOG("bring-up stopped: stage \"%s\" failed (%d)", stages[i].name, rc);
	else
		LOG("bring-up finished");

	_step     = "finished";
	_finished = true;

	return 0;
}


void lx_user_init(void)
{
	kthread_run(watch_thread,   NULL, "pixel_audio_watch");
	kthread_run(bringup_thread, NULL, "pixel_audio_bringup");
}
