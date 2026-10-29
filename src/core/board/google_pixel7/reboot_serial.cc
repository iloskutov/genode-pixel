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
 * The reboot reason and where it is stored are those of the vendor kernel's
 * reboot driver for the GS201 (pixel-reboot).
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* core includes */
#include <platform.h>
#include <board.h>

/* base-hw internal includes */
#include <hw/spec/arm_64/psci_call.h>


/*
 * The boot loader takes the boot mode from the storage tag RSBM of the
 * MAX77759's MAXQ controller ("Reboot mode: 0xfc - Bootloader"), the PMU
 * register alone is not enough. The vendor's pixel-reboot driver writes
 * both, RSBM via gbms_storage_write. The MAXQ is reached via the PMIC (I2C
 * address 0x66) on the bus of USI13, whose controller is polled here as by
 * the I2C driver (src/driver/i2c/gs201/hsi2c.h). Clock and timing of the
 * controller are left as the boot loader or the I2C driver set them up.
 */
namespace Rsbm {

	using namespace Genode;

	static uint64_t counter()
	{
		uint64_t v;
		asm volatile ("isb; mrs %0, cntpct_el0" : "=r"(v));
		return v;
	}

	static uint64_t counter_hz()
	{
		uint64_t v;
		asm volatile ("mrs %0, cntfrq_el0" : "=r"(v));
		return v;
	}

	static void delay_us(uint64_t us)
	{
		uint64_t const end = counter() + us * counter_hz() / 1000000;
		while (counter() < end);
	}

	struct Hsi2c
	{
		enum : unsigned {
			CTL = 0x00, FIFO_CTL = 0x04, TRAILING_CTL = 0x08,
			INT_ENABLE = 0x20, INT_STATUS = 0x24, FIFO_STATUS = 0x30,
			TX_DATA = 0x34, RX_DATA = 0x38, CONF = 0x40, AUTO_CONF = 0x44,
			TIMEOUT = 0x48, TRANS_STATUS = 0x50, TIMING_FS1 = 0x60,
			TIMING_FS2 = 0x64, TIMING_FS3 = 0x68, ADDR = 0x70, USI_CON = 0xc4,
		};

		enum : uint32_t {
			CTL_MASTER = 1u << 3, CTL_RXCHON = 1u << 6, CTL_TXCHON = 1u << 7,
			INT_TRANSFER_DONE = 1u << 7, INT_NODEV = 1u << 10,
			INT_ERROR_MASK = 0xfu << 8,
			FIFO_TX_LVL_MASK = 0x7f, FIFO_TX_EMPTY = 1u << 8,
			FIFO_RX_EMPTY = 1u << 24,
			CONF_AUTO_MODE = 1u << 31,
			AUTO_READ = 1u << 16, AUTO_STOP = 1u << 17,
			AUTO_MASTER_RUN = 1u << 31,
			TIMEOUT_EN = 1u << 31,
			FIFO_SIZE = 16, FIFO_TRIGGER_MAX = 8,
		};

		addr_t const base;

		uint32_t read(unsigned off) {
			return *(uint32_t volatile *)(base + off); }

		void write(unsigned off, uint32_t v) {
			*(uint32_t volatile *)(base + off) = v; }

		void clear_pending() { write(INT_STATUS, read(INT_STATUS)); }

		void init()
		{
			write(USI_CON, 0);
			write(INT_ENABLE, 0);
			clear_pending();
			write(CTL, CTL_MASTER);
			write(CONF, read(CONF) | CONF_AUTO_MODE);
		}

		/* one message in auto mode, polled, as Hsi2c::Master::message */
		bool message(uint8_t address, bool rd, uint8_t *bytes, unsigned len,
		             bool stop)
		{
			unsigned const level = len > FIFO_TRIGGER_MAX ? FIFO_TRIGGER_MAX : len;

			write(TIMEOUT, read(TIMEOUT) & ~TIMEOUT_EN);
			write(TRAILING_CTL, 0xffffff);
			write(FIFO_CTL, 0x3 | (level << 4) | (level << 16));
			clear_pending();
			write(ADDR, (read(ADDR) & ~((0x3ffu << 10) | 0x3ffu | (0xffu << 24)))
			            | (0x7u << 24) | ((uint32_t)(address & 0x7f) << 10));
			write(CTL, (read(CTL) & ~(CTL_RXCHON | CTL_TXCHON))
			           | (rd ? CTL_RXCHON : CTL_TXCHON));
			write(INT_ENABLE, INT_TRANSFER_DONE | INT_ERROR_MASK);
			uint32_t const conf = (read(AUTO_CONF) & ~(0xffffu | AUTO_READ | AUTO_STOP | AUTO_MASTER_RUN))
			                    | len | (rd ? AUTO_READ : 0u) | (stop ? AUTO_STOP : 0u);
			write(AUTO_CONF, conf);
			write(AUTO_CONF, conf | AUTO_MASTER_RUN);

			uint64_t const deadline = counter() + counter_hz() / 10;   /* 100 ms */
			unsigned done = 0;
			for (;;) {
				uint32_t const status = read(INT_STATUS);
				if (status & (INT_NODEV | INT_ERROR_MASK)) {
					raw("RSBM: I2C ", rd ? "read" : "write", " of 0x", Hex(address),
					    " failed after ", done, " bytes, INT_STATUS=", Hex(status),
					    " TRANS_STATUS=", Hex(read(TRANS_STATUS)));
					return false;
				}
				if (rd) {
					while (done < len && !(read(FIFO_STATUS) & FIFO_RX_EMPTY))
						bytes[done++] = (uint8_t)read(RX_DATA);
					if (done == len) {
						uint64_t const end = counter() + counter_hz() / 200;
						while (!(read(INT_STATUS) & INT_TRANSFER_DONE) && counter() < end);
						break;
					}
				} else {
					while (done < len && (read(FIFO_STATUS) & FIFO_TX_LVL_MASK) < FIFO_SIZE)
						write(TX_DATA, bytes[done++]);
					if (done == len && (status & INT_TRANSFER_DONE)
					 && (read(FIFO_STATUS) & FIFO_TX_EMPTY))
						break;
				}
				if (counter() >= deadline) {
					raw("RSBM: I2C ", rd ? "read" : "write", " of 0x", Hex(address),
					    " timed out after ", done, " bytes, INT_STATUS=", Hex(status),
					    " TRANS_STATUS=", Hex(read(TRANS_STATUS)));
					return false;
				}
				delay_us(50);
			}
			write(INT_ENABLE, 0);
			clear_pending();
			return true;
		}
	};

	enum : uint8_t {
		PMIC_ADDRESS      = 0x66,
		AP_DATAOUT0       = 0x81,   /* request, 32 bytes */
		AP_DATAOUT32      = 0xa1,   /* writing it starts the request */
		AP_DATAIN0        = 0xb1,   /* response, 32 bytes */
		OPCODE_USER_WRITE = 0x82,   /* offset, length, data */
	};

	/* write the boot mode to RSBM (user space offset 0, 4 bytes) */
	static void write(uint8_t mode)
	{
		Hsi2c i2c { Core::Platform::mmio_to_virt(Board::PMIC_I2C_BASE) };

		raw("RSBM: USI_CON=", Hex(i2c.read(Hsi2c::USI_CON)),
		    " CTL=", Hex(i2c.read(Hsi2c::CTL)), " CONF=", Hex(i2c.read(Hsi2c::CONF)),
		    " FS1=", Hex(i2c.read(Hsi2c::TIMING_FS1)), " FS2=", Hex(i2c.read(Hsi2c::TIMING_FS2)),
		    " FS3=", Hex(i2c.read(Hsi2c::TIMING_FS3)));

		i2c.init();

		uint8_t request[33] { AP_DATAOUT0, OPCODE_USER_WRITE, 0, 4,
		                      mode, 0, 0, 0x80 };
		uint8_t start[2]    { AP_DATAOUT32, 0 };
		uint8_t response[4] { };
		uint8_t reg = AP_DATAIN0;

		if (!i2c.message(PMIC_ADDRESS, false, request, sizeof(request), true)
		 || !i2c.message(PMIC_ADDRESS, false, start, sizeof(start), true))
			return;

		/* the vendor driver's poll mode waits as long */
		delay_us(200000);

		if (!i2c.message(PMIC_ADDRESS, false, &reg, 1, false)
		 || !i2c.message(PMIC_ADDRESS, true, response, sizeof(response), true))
			return;

		raw("RSBM: response ", Hex(response[0]), " ", Hex(response[1]), " ",
		    Hex(response[2]), " ", Hex(response[3]),
		    response[0] == OPCODE_USER_WRITE ? " (ok)" : " (opcode mismatch)");
	}
}


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

	Rsbm::write(REBOOT_REASON_FASTBOOT);

	/* let the UART send the lines of the RSBM write */
	Rsbm::delay_us(100000);

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
