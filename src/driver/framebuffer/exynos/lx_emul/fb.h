/**
 * \brief  Lx_emul support to register Linux kernel framebuffer
 * \author Stefan Kalkowski
 * \date   2021-05-17
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#ifndef _LX_EMUL__FB_H_
#define _LX_EMUL__FB_H_

#ifdef __cplusplus
extern "C" {
#endif

void lx_emul_framebuffer_ready(void * base, unsigned long size,
                               unsigned xres, unsigned yres);

/* paint captured changes (Genode side), non-zero if the framebuffer changed;
 * 'may_stop': stop capturing when nothing changed, until the wakeup */
int lx_emul_framebuffer_capture(int may_stop);

/* nitpicker's wakeup after 'capture_stopped': resume the update task */
void lx_emul_framebuffer_wakeup(void);

/* switch the display on or off (DPMS) */
void lx_emul_framebuffer_power(bool on);

/* set the panel brightness, percent (1..100) of its normal range */
void lx_emul_framebuffer_brightness(unsigned percent);

#ifdef __cplusplus
}
#endif

#endif /* _LX_EMUL__FB_H_ */
