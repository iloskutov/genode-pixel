/*
 * \brief  Interface between the Genode side and the Linux side of pixel_audio
 * \author Ivan Loskutov
 * \date   2026-10-01
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_AUDIO_H_
#define _PIXEL_AUDIO_H_

#ifdef __cplusplus
extern "C" {
#endif

struct pixel_audio_config
{
	unsigned nop_poll_ms;         /* period of the Trusty NOP poller, 0 = off */
	unsigned online_timeout_ms;   /* wait for the AoC to come online */

	int      verbose;             /* log every control write */

	/*
	 * Forwarding of the AoC firmware log (aoc_log.c): on/off, lines per
	 * second after a burst of 'aoc_log_burst' lines, whether the replies of
	 * commands that succeeded are printed too
	 */
	int      aoc_log;
	unsigned aoc_log_rate;
	unsigned aoc_log_burst;
	int      aoc_log_replies;

};

extern struct pixel_audio_config pixel_audio_config;

/**
 * Forward an SMC to the secure monitor (main.cc)
 *
 * The function id and the arguments travel in a[0..7] (x0..x7), the result
 * comes back in r[0..3] (x0..x3), as SMCCC defines it.
 */
void pixel_audio_smc(unsigned long const a[8], unsigned long r[4]);

/**
 * Return the address of a virtual-memory window of 'size' bytes that is
 * backed by nothing: every access raises an unresolvable page fault
 */
void *pixel_audio_unbacked_window(unsigned long size);

#ifdef __cplusplus
}
#endif

#endif /* _PIXEL_AUDIO_H_ */
