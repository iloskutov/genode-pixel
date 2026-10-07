/*
 * \brief  Board definitions for Google Pixel 7
 * \author Piotr Tworek
 * \author Ivan Loskutov
 * \date   2026-02-06
 *
 * Derived from the following file of the Genode OS framework and adapted to
 * the Google Pixel 7:
 *
 *   repos/base-hw/src/include/hw/spec/arm/virt_qemu_board.h
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU Affero General Public
 * License version 3, see the file LICENSE.AGPL.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef _SRC__INCLUDE__HW__SPEC__ARM_64__GOOGLE_PIXEL7_BOARD_H_
#define _SRC__INCLUDE__HW__SPEC__ARM_64__GOOGLE_PIXEL7_BOARD_H_

#include <drivers/uart/samsung_exynos.h>
#include <hw/spec/arm/boot_info.h>

namespace Hw::Google_pixel7_board {

	using Serial = Genode::Samsung_exynos_uart;

	enum {
		RAM_BASE   = 0x80000000,
		RAM_SIZE   = 0x80000000, /* 2 GiB (first memory bank)
		                          * bank 2: 0x880000000-0x9ffffffff (6 GiB) */

		UART_BASE  = 0x10a00000, /* samsung,exynos-uart */
		UART_SIZE  = 0x100,
		UART_CLOCK = 0,

		CACHE_LINE_SIZE_LOG2 = 6, /* 64 bytes */
	};

	static constexpr Genode::size_t NR_OF_CPUS = 8;

	namespace Cpu_mmio {
		enum {
			IRQ_CONTROLLER_DISTR_BASE   = 0x10400000,
			IRQ_CONTROLLER_DISTR_SIZE   = 0x10000,
			IRQ_CONTROLLER_CPU_BASE     = 0x10410000,
			IRQ_CONTROLLER_CPU_SIZE     = 0x10000,
			IRQ_CONTROLLER_REDIST_BASE  = 0x10440000,
			IRQ_CONTROLLER_REDIST_SIZE  = 0x100000, /* 8 CPUs * 128 KiB */

			/*
			 * The memory-mapped virtual CPU interface is a concept of GICv2.
			 * With GICv3, the virtual interface is accessed via system
			 * registers. The size of 0 lets core's 'attach_pic' map nothing
			 * into a guest.
			 */
			IRQ_CONTROLLER_VT_CPU_BASE = 0,
			IRQ_CONTROLLER_VT_CPU_SIZE = 0,
		};
	};
};

#endif /* _SRC__INCLUDE__HW__SPEC__ARM_64__GOOGLE_PIXEL7_BOARD_H_ */
