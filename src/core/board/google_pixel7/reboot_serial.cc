/*
 * \brief  Serial reset monitor of the Google Pixel 7 (REBOOT=serial)
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * The line "RST" received at the kernel's UART reboots the phone into its
 * boot loader, where the next image can be started via fastboot. The UART
 * is polled whenever the kernel handles an interrupt, at any CPU. The
 * kernel does this with its global lock held, which also protects the state
 * of the monitor. The characters that arrive in between are kept by the
 * receive FIFO, which the boot loader leaves enabled.
 *
 * The reboot reason and its register are those of the vendor kernel's reboot
 * driver for the GS201.
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* core includes */
#include <platform.h>
#include <board.h>

/* base-hw internal includes */
#include <hw/spec/arm_64/psci_call.h>


[[noreturn]] static void reboot_into_boot_loader()
{
	enum : Genode::umword_t {

		/*
		 * The boot loader decides what to boot by a reboot reason in a
		 * register of the PMU that survives the reset. The register is
		 * writeable by the secure monitor only.
		 */
		SMC_CMD_PRIV_REG       = 0x82000504,
		PRIV_REG_WRITE         = 1,
		PMU_REBOOT_REASON      = 0x18060810,
		REBOOT_REASON_FASTBOOT = 0xfc,

		PSCI_SYSTEM_RESET      = 0x84000009,
	};

	Hw::Psci_smc_functor::call(SMC_CMD_PRIV_REG, PMU_REBOOT_REASON,
	                           PRIV_REG_WRITE, REBOOT_REASON_FASTBOOT);

	Hw::Psci_smc_functor::call(PSCI_SYSTEM_RESET, 0, 0, 0);

	for (;;);
}


void Board::poll_serial_reset()
{
	static constexpr char     sequence[] = "RST";
	static constexpr unsigned length     = sizeof(sequence) - 1;

	static Board::Serial serial { Core::Platform::mmio_to_virt(Board::UART_BASE),
	                              Board::UART_CLOCK, 115200 };
	static unsigned matched = 0;

	while (serial.char_avail()) {

		char const c = serial.get_char();

		if (matched == length && (c == '\n' || c == '\r'))
			reboot_into_boot_loader();

		if (matched < length && c == sequence[matched])
			matched++;
		else
			matched = (c == sequence[0]) ? 1 : 0;
	}
}
