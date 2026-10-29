/*
 * \brief  Stand-ins for the parts of aoc_alsa that are not built
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * Not built: aoc_alsa_compr.c (compress offload), aoc_alsa_incall.c
 * (in-call music and call recording), aoc_alsa_voip.c, aoc_alsa_usb.c and
 * aoc_alsa_dp.c. Their
 * platform drivers do not exist, the device tree of the driver points the
 * front-end links that used them to the plain PCM platform so that the
 * sound card registers and the PCM numbers keep their order. Such a PCM is
 * not usable: its period interrupts end in the handlers below.
 *
 * Every function that stands in for behaviour says so in the log once.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <lx_emul.h>

#include "aoc_alsa.h"
#include "aoc_alsa_drv.h"


static void stubbed(bool *reported, char const *what)
{
	if (*reported)
		return;

	*reported = true;
	printk("pixel_audio: aoc_alsa: %s is not part of this driver (stub)\n",
	       what);
}


/*
 * Registration of the platform drivers, called by aoc_alsa_init
 */

int aoc_compr_init(void)
{
	return 0;
}


int aoc_incall_init(void) { return 0; }
int aoc_voip_init(void)   { return 0; }
int aoc_usb_init(void)    { return 0; }
int aoc_dp_init(void)     { return 0; }


/*
 * Service interrupts. The compress-offload handler is installed for the
 * service "audio_playback6" when aoc_alsa binds it; the other two when a
 * placeholder PCM of an in-call, hi-fi or VoIP service is opened.
 */

void aoc_compr_offload_isr(struct aoc_service_dev *dev)
{
	static bool reported;
	stubbed(&reported, "compress-offload interrupt handling");
}


void aoc_voip_isr(struct aoc_service_dev *dev)
{
	static bool reported;
	stubbed(&reported, "VoIP period handling");
}


void aoc_incall_hifi_isr(struct aoc_service_dev *dev)
{
	static bool reported;
	stubbed(&reported, "in-call/hi-fi period handling");
}


/*
 * Control "Offload Position": only reached with a compress stream, which
 * cannot exist (chip->compr_offload_stream stays NULL)
 */

int aoc_compr_get_position(struct aoc_alsa_stream *alsa_stream,
                           uint64_t *position)
{
	static bool reported;
	stubbed(&reported, "compress-offload position");
	return -ENODEV;
}


int aoc_compr_offload_reset_io_sample_base(struct aoc_alsa_stream *alsa_stream)
{
	static bool reported;
	stubbed(&reported, "compress-offload position reset");
	return -ENODEV;
}


/*
 * Control "USB Memory Config": passes the address of the xHCI device
 * context to the AoC for USB audio offload. There is no USB host in this
 * component.
 */
int aoc_set_usb_mem_config(struct aoc_chip *achip)
{
	static bool reported;
	stubbed(&reported, "USB audio offload (control \"USB Memory Config\")");
	return -ENODEV;
}


/*
 * audiometrics (google_amplifiers/audiometrics, not built): aoc_alsa
 * registers a callback through which the metrics driver asks for the state
 * of a PDM microphone. Nobody asks.
 */
void pdm_callback_register(pdm_callback callback, int pdm_number, void *pdm_priv)
{
}
