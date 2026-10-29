/*
 * \brief  Bring-up step of the CS35L41 amplifiers on SPI7
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * Creates the held-back SPI7 controller device. Its probe registers the two
 * amplifier SPI devices, whose probe (reset pulse, OTP boot, device and
 * revision id, OTP trim read, interrupt request) runs synchronously. The
 * step needs neither Trusty nor the AoC; it is the first part of stage
 * "card" and logs its own result line (stage "amp").
 *
 * A failed amplifier probe leaves a "nop" codec behind (vendor code), so
 * that the sound card still registers: the result line says which of the
 * two amplifiers answered.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>

#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <sound/soc.h>

#include "wm_adsp.h"
#include "cs35l41.h"
#include "sound/cs35l41-private.h"

#define LOG(fmt, ...) printk("pixel_audio: " fmt "\n", ##__VA_ARGS__)

struct amp_state
{
	char const  *spi_name;   /* device name on the SPI bus */
	char const  *role;
	bool         present;    /* SPI device exists      */
	bool         bound;      /* driver bound           */
	bool         probed;     /* full probe, not "nop"  */
	unsigned     devid, revid;
	int          id_rc;
};


static void amp_query(struct amp_state *amp)
{
	struct device          *dev;
	struct cs35l41_private *cs35l41;

	dev = bus_find_device_by_name(&spi_bus_type, NULL, amp->spi_name);
	if (!dev)
		return;

	amp->present = true;
	amp->bound   = dev->driver != NULL;

	cs35l41 = amp->bound ? dev_get_drvdata(dev) : NULL;

	if (cs35l41 && !IS_ERR_OR_NULL(cs35l41->regmap)) {

		/*
		 * The work queue is created by the last step of cs35l41_probe; a
		 * probe that ended in the "nop" codec leaves it unset.
		 */
		amp->probed = cs35l41->wq != NULL;

		/* two register reads, as the probe itself does */
		amp->id_rc = regmap_read(cs35l41->regmap, CS35L41_DEVID, &amp->devid);
		if (!amp->id_rc)
			amp->id_rc = regmap_read(cs35l41->regmap, CS35L41_REVID,
			                         &amp->revid);
	}
	put_device(dev);
}


/**
 * Probe the SPI7 controller and the two amplifiers
 *
 * \return number of amplifiers that completed their probe (0..2), or a
 *         negative error code if the SPI controller did not probe
 */
int pixel_audio_stage_amp(void)
{
	static struct amp_state amps[] = {
		{ .spi_name = "spi7.0", .role = "top/earpiece" },
		{ .spi_name = "spi7.1", .role = "bottom"       },
	};

	struct platform_device *spi;
	unsigned i, ok = 0;

	pixel_audio_set_step("amp: probe of " PIXEL_AUDIO_DT_SPI7
	                     " and of the two CS35L41");

	spi = pixel_audio_create_device(PIXEL_AUDIO_DT_SPI7);
	if (!spi || !spi->dev.driver) {
		LOG("stage amp FAILED: probe of %s (device %s, driver %s): "
		    "no SPI bus, the amplifiers are not probed",
		    PIXEL_AUDIO_DT_SPI7, spi ? "created" : "not created",
		    spi && spi->dev.driver ? "bound" : "not bound");
		return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(amps); i++) {
		amp_query(&amps[i]);
		if (amps[i].probed)
			ok++;
	}

	for (i = 0; i < ARRAY_SIZE(amps); i++) {
		struct amp_state const *a = &amps[i];

		if (!a->present)
			LOG("amp: %s (%s): no SPI device (device-tree node disabled or "
			    "chip select not available)", a->spi_name, a->role);
		else if (!a->bound)
			LOG("amp: %s (%s): driver not bound (probe deferred or failed)",
			    a->spi_name, a->role);
		else if (!a->probed)
			LOG("amp: %s (%s): probe failed, nop codec, device id %#x "
			    "revision %#x (read rc %d; expected 0x35a40, 0xb2)",
			    a->spi_name, a->role, a->devid, a->revid, a->id_rc);
	}

	if (ok == ARRAY_SIZE(amps)) {
		LOG("stage amp ok: both CS35L41 probed: spi7.0 id %#x revision %#x, "
		    "spi7.1 id %#x revision %#x", amps[0].devid, amps[0].revid,
		    amps[1].devid, amps[1].revid);
	} else {
		LOG("stage amp FAILED: %u of %u CS35L41 probed (spi7.0 %s, spi7.1 "
		    "%s), see the cs35l41 messages above", ok,
		    (unsigned)ARRAY_SIZE(amps),
		    amps[0].probed ? "ok" : "failed",
		    amps[1].probed ? "ok" : "failed");
	}
	return ok;
}
