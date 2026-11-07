/*
 * \brief  Linux-side definitions of the pixel_modem driver
 * \author Ivan Loskutov
 * \date   2026-11-07
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_MODEM__LX_EMUL_H_
#define _PIXEL_MODEM__LX_EMUL_H_

/* Needed to trace and stop */
#include <lx_emul/debug.h>

/* fix for wait_for_completion_timeout where the __sched include is missing */
#include <linux/sched/debug.h>

/* fix for missing include in linux/dynamic_debug.h */
#include <linux/compiler_attributes.h>

/* fix for missing include in linux/irqdesc.h */
#include <linux/irq.h>

#endif /* _PIXEL_MODEM__LX_EMUL_H_ */
