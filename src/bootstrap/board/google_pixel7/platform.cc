/*
 * \brief   Platform implementations specific for base-hw and Google Pixel 7
 * \author  Piotr Tworek
 * \author  Ivan Loskutov
 * \date    2026-02-06
 *
 * Derived from the following file of the Genode OS framework and adapted to
 * the Google Pixel 7:
 *
 *   repos/base-hw/src/bootstrap/board/virt_qemu_arm_v8a/platform.cc
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU Affero General Public
 * License version 3, see the file LICENSE.AGPL.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include <platform.h>

#include "watchdog.h"

extern "C" void   pixel7_sanitize_cpu_state();
extern "C" void * _pixel7_start_secondary;


/*
 * Release a PHY from its isolation
 *
 * The isolation bit (bit 0) is in a register of the power-management unit
 * that only the secure monitor may write. Its call SMC_CMD_PRIV_REG reads
 * (option 0) and writes (option 1) such a register. The vendors' PHY
 * drivers do it when they power a PHY up, which a driver cannot do from
 * user level. The isolation is never set again.
 */
static void release_phy_isolation(unsigned pmu_register, char const *name)
{
	enum : unsigned {
		SMC_CMD_PRIV_REG = 0x82000504,
		PRIV_REG_READ    = 0,
		PRIV_REG_WRITE   = 1,
	};

	using Smc = Hw::Psci_smc_functor;

	int const value = Smc::call(SMC_CMD_PRIV_REG, pmu_register,
	                            PRIV_REG_READ, 0);

	if (Smc::call(SMC_CMD_PRIV_REG, pmu_register, PRIV_REG_WRITE,
	              (unsigned)value | 1))
		Genode::error("failed to release the isolation of the ", name, " PHY");
}


Bootstrap::Platform::Board::Board()
:
	early_ram_regions(
		/*
		 * bank 1: 0x80000000-0xffffffff, without the 'no-map' reserved
		 * memory of the device tree: the security chip (gsa@90200000,
		 * 4 MiB), the AoC DSP (aoc@94000000, 48 MiB), the modem (cp_rmem_1,
		 * cp_rmem, cp_msi_rmem), and the boot loader's framebuffer
		 * (0xfac00000, 10 MiB), which the display keeps scanning out
		 */
		Memory_region { 0x80000000, 0x10200000 },  /* before gsa */
		Memory_region { 0x90600000, 0x03a00000 },  /* after gsa, before aoc */
		Memory_region { 0x97000000, 0x51000000 },  /* after aoc, before cp_rmem_1 */
		Memory_region { 0xea000000, 0x00400000 },  /* between cp_rmem_1 and cp_rmem */
		Memory_region { 0xeac00000, 0x0b600000 },  /* after cp_rmem, before cp_msi_rmem */
		Memory_region { 0xf6201000, 0x049ff000 },  /* after cp_msi_rmem, before fb */
		Memory_region { 0xfb600000, 0x04a00000 },  /* after the framebuffer */

		/* bank 2: 0x880000000-0x9ffffffff, 6 GiB */
		Memory_region { 0x880000000UL, 0x180000000UL }),
	late_ram_regions(Memory_region { }),
	core_mmio(Memory_region { ::Board::UART_BASE, ::Board::UART_SIZE },
	          Memory_region { ::Board::Cpu_mmio::IRQ_CONTROLLER_DISTR_BASE,
	                          ::Board::Cpu_mmio::IRQ_CONTROLLER_DISTR_SIZE },
	          Memory_region { ::Board::Cpu_mmio::IRQ_CONTROLLER_REDIST_BASE,
	                          ::Board::Cpu_mmio::IRQ_CONTROLLER_REDIST_SIZE })
{
	/* boot CPU, the secondary CPUs do it at their entry */
	pixel7_sanitize_cpu_state();

	Watchdog::stop();

	/* PMU registers of the vendor's device tree (pmu_offset of the PHYs) */
	release_phy_isolation(0x18063ec4, "PCIe1");   /* WiFi */
	release_phy_isolation(0x18063eb0, "USB");
	release_phy_isolation(0x18063eb4, "USB DP");
}


/*
 * The secondary CPUs do not enter bootstrap at the generic entry point but
 * at '_pixel7_start_secondary', which continues there.
 */
void Board::Cpu::wake_up_all_cpus(void *)
{
	for (unsigned cpu_id = 1; cpu_id < NR_OF_CPUS; cpu_id++) {

		/* each CPU is its own cluster: Aff1 = cpu_id, Aff0 = 0 */
		if (!Board::Psci::cpu_on(cpu_id << 8, &_pixel7_start_secondary))
			Genode::error("Failed to boot CPU", cpu_id);
	}
}
