/*
 * \brief  GICv3 with virtualization extensions for DynamIQ SoCs
 * \author Stefan Kalkowski
 * \author Ivan Loskutov
 * \date   2026-02-07
 *
 * Derived from the following file of the Genode OS framework and adapted to
 * the Google Pixel 7:
 *
 *   repos/base-hw/src/core/spec/arm/virtualization/gicv3.h
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU Affero General Public
 * License version 3, see the file LICENSE.AGPL.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef _CORE__BOARD__GOOGLE_PIXEL7__GICV3_H_
#define _CORE__BOARD__GOOGLE_PIXEL7__GICV3_H_

#include <hw/spec/arm/gicv3.h>

namespace Board {

	using Hw::Global_interrupt_controller;

	class Local_interrupt_controller;
	class Cpu;

	/**
	 * Reboot into the boot loader if requested via the kernel's UART
	 *
	 * Implemented by 'reboot_serial.cc' or 'reboot_none.cc', selected by the
	 * build argument REBOOT.
	 */
	void poll_serial_reset();
};


class Board::Local_interrupt_controller : public Hw::Local_interrupt_controller
{
	public:

		using uint32_t = Genode::uint32_t;
		using uint64_t = Genode::uint64_t;

		struct Virtual_context
		{
			uint64_t lr    { 0 };
			uint32_t apr   { 0 };
			uint32_t vmcr  { 0x4c0000 };
			uint32_t misr  { 0 };
			uint32_t eisr  { 0 };
			uint32_t elrsr { 0xffffffff };
		};

		bool ack_virtual_irq(Virtual_context &c)
		{
			if (!(c.eisr & 1)) return false;

			c.lr    = 0;
			c.elrsr = 0xffffffff;
			c.misr  = 0;
			c.eisr  = 0;
			return true;
		}

		void insert_virtual_irq(Virtual_context &c, unsigned irq)
		{
			enum { SPURIOUS = 1023 };

			if (irq == SPURIOUS || c.lr) return;

			c.lr = irq | 1ULL << 41 | 1ULL << 60 | 1ULL << 62;
		}

		/**
		 * Take pending interrupt request
		 *
		 * Called by the kernel whenever it handles an interrupt, which
		 * makes it the hook for the serial reset monitor.
		 */
		bool take_request(unsigned &irq)
		{
			poll_serial_reset();

			return Hw::Local_interrupt_controller::take_request(irq);
		}

		/**
		 * Send inter-processor interrupt
		 *
		 * On DynamIQ SoCs (MPIDR MT=1), each CPU resides in its own
		 * cluster: Aff1 = cpu_id, Aff0 = 0. The ICC_SGI1R_EL1 format
		 * requires the target Aff1 in bits [23:16] and the TargetList
		 * bitmask (for Aff0) in bits [15:0].
		 */
		void send_ipi(Hw::Arm_cpu::Id cpu_id)
		{
			Cpu_interface::Icc_sgi1r_el1::write(
				((uint64_t)cpu_id.value << 16) | 1);
		}

		Local_interrupt_controller(Global_interrupt_controller &gic, Cpu &)
		: Hw::Local_interrupt_controller(gic) { }
};

#endif /* _CORE__BOARD__GOOGLE_PIXEL7__GICV3_H_ */
