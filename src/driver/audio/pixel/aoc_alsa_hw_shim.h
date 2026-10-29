/*
 * \brief  Compile-time adaptation of the vendor aoc_alsa_hw.c (force-included)
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * aoc_alsa_hw.c runs unmodified. 'aoc_audio_control' waits for the reply to
 * an AoC command in a loop that only compares against 'jiffies':
 *
 *   while ((aoc_service_read(...) < 1) && time_is_after_jiffies(expired))
 *           count++;
 *
 * In Linux the timer interrupt advances jiffies while the loop spins. Under
 * lx_emul the loop is never interrupted, jiffies stand still and a missing
 * reply would hang the driver instead of running into the 500 ms timeout.
 * The comparison is therefore preceded by 'cpu_relax', which updates jiffies
 * in the lx_emul shadow (asm/vdso/processor.h). The reply itself needs no
 * interrupt: the AoC writes it into the shared ring.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_AUDIO__AOC_ALSA_HW_SHIM_H_
#define _PIXEL_AUDIO__AOC_ALSA_HW_SHIM_H_

#include <linux/jiffies.h>
#include <asm/processor.h>

#undef  time_is_after_jiffies
#define time_is_after_jiffies(a) ({ cpu_relax(); time_before(jiffies, a); })

#endif /* _PIXEL_AUDIO__AOC_ALSA_HW_SHIM_H_ */
