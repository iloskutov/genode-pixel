/*
 * \brief  Definitions needed by the dummies of the panthor GPU driver
 * \author Ivan Loskutov
 * \date   2026-10-07
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

/* Needed to trace and stop */
#include <lx_emul/debug.h>

/* fix for wait_for_completion_timeout where the __sched include is missing */
#include <linux/sched/debug.h>

/* fix for missing include in linux/dynamic_debug.h */
#include <linux/compiler_attributes.h>

/* fix for missing include in linux/irqdesc.h */
#include <linux/irq.h>

#include <linux/platform_device.h>

/* forward declarations for generated_dummies.c */
struct drm_device;
struct drm_file;
