/*
 * \brief  Board driver for bootstrap
 * \author Piotr Tworek
 * \author Ivan Loskutov
 * \date   2026-02-06
 *
 * Derived from the following file of the Genode OS framework and adapted to
 * the Google Pixel 7:
 *
 *   repos/base-hw/src/bootstrap/board/virt_qemu_arm_v8a/board.h
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU Affero General Public
 * License version 3, see the file LICENSE.AGPL.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef _BOOTSTRAP__SPEC__GOOGLE_PIXEL7__BOARD_H_
#define _BOOTSTRAP__SPEC__GOOGLE_PIXEL7__BOARD_H_

#include <hw/spec/arm_64/google_pixel7_board.h>
#include <hw/spec/arm/gicv3.h>
#include <hw/spec/arm/lpae.h>
#include <hw/spec/arm_64/cpu.h>
#include <hw/spec/arm_64/psci_call.h>

namespace Board {

	using namespace Hw::Google_pixel7_board;

	using Psci = Hw::Psci<Hw::Psci_smc_functor>;

	struct Cpu : Hw::Arm_64_cpu
	{
		/*
		 * The generic code leaves the granule size of the upper address
		 * range (TG1) at 0, which is a reserved encoding. Cortex-A55 takes
		 * it as 4K, Cortex-A78 and Cortex-X1 do not.
		 */
		struct Tcr_el1 : Hw::Arm_64_cpu::Tcr_el1
		{
			static void write(access_t v)
			{
				Tg1::set(v, 0b10); /* 4K */
				Hw::Arm_64_cpu::Tcr_el1::write(v);
			}
		};

		/*
		 * The content of the TLB is UNKNOWN after a CPU got powered on, and
		 * the firmware may have used the CPU with its own page tables. The
		 * generic code enables the MMUs of EL2 and EL1 without invalidating
		 * the TLB, and does not synchronize the enabling of the EL1 MMU.
		 * Without both, the Cortex-A78 and Cortex-X1 cores take an exception
		 * right after the MMU is on.
		 */
		struct Sctlr_el2 : Hw::Arm_64_cpu::Sctlr_el2
		{
			static void write(access_t v)
			{
				asm volatile("dsb sy     \n"
				             "tlbi alle2 \n"
				             "dsb sy     \n"
				             "isb");
				Hw::Arm_64_cpu::Sctlr_el2::write(v);
			}
		};

		struct Sctlr_el1 : Hw::Arm_64_cpu::Sctlr_el1
		{
			static void write(access_t v)
			{
				asm volatile("dsb sy       \n"
				             "tlbi vmalle1 \n"
				             "dsb sy       \n"
				             "isb");
				Hw::Arm_64_cpu::Sctlr_el1::write(v);
				asm volatile("isb");
			}
		};

		static void wake_up_all_cpus(void*);
	};

	static constexpr Genode::size_t NR_OF_CPUS = 8;

	using Hw::Global_interrupt_controller;
	using Hw::Local_interrupt_controller;
};

#endif /* _BOOTSTRAP__SPEC__GOOGLE_PIXEL7__BOARD_H_ */
