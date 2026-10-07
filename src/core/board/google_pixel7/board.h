/*
 * \brief  Board driver for core
 * \author Piotr Tworek
 * \author Ivan Loskutov
 * \date   2026-02-06
 *
 * Derived from the following file of the Genode OS framework and adapted to
 * the Google Pixel 7:
 *
 *   repos/base-hw/src/core/board/virt_qemu_arm_v8a/board.h
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU Affero General Public
 * License version 3, see the file LICENSE.AGPL.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef _CORE__SPEC__GOOGLE_PIXEL7__BOARD_H_
#define _CORE__SPEC__GOOGLE_PIXEL7__BOARD_H_

/* base-hw internal includes */
#include <hw/spec/arm_64/google_pixel7_board.h>
#include <spec/arm/generic_timer.h>
#include <gicv3.h>
#include <spec/arm_64/cpu/vcpu_state_virtualization.h>
#include <spec/arm/virtualization/board.h>
#include <kernel/configuration.h>
#include <kernel/irq.h>
#include <spec/arm_v8/cpu.h>

namespace Board {

	using namespace Hw::Google_pixel7_board;

	static constexpr Genode::size_t NR_OF_CPUS = 8;

	enum {
		TIMER_IRQ           = 30, /* PPI IRQ 14 */
		VT_TIMER_IRQ        = 11 + 16,
		VT_MAINTAINANCE_IRQ = 9  + 16,
		VCPU_MAX            = 16
	};
};

#endif /* _CORE__SPEC__GOOGLE_PIXEL7__BOARD_H_ */
