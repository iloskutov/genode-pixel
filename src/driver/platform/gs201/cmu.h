/*
 * \brief  Clocks of the GS201 clock-management units
 * \author Ivan Loskutov
 * \date   2026-02-08
 *
 * The register offsets are taken from the vendor's clock description of the
 * GS201 (cmucal-sfr.c) and its device tree. All clocks are fed by the
 * 26 MHz oscillator. Only the clocks of the drivers of this repository are
 * described: the bus clocks of the PERIC1 block, the USI0 serial interface
 * in SPI mode (touchscreen), and the UFS host controller.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _SRC__DRIVER__PLATFORM__GS201__CMU_H_
#define _SRC__DRIVER__PLATFORM__GS201__CMU_H_

#include <os/attached_mmio.h>
#include <clock.h>

namespace Driver { struct Cmu; }


struct Driver::Cmu : private Noncopyable
{
	Genode::Env &_env;
	Clocks      &_clocks;
	Clock       &_osc_clk;

	/*
	 * Number of times a busy bit is polled after a change. It is a bound for
	 * the busy waiting, not a time.
	 */
	enum { MAX_BUSY_POLLS = 1000 };

	/*
	 * Clock gate
	 *
	 * Enabling sets the manual mode (bit 20) and the gate value (bit 21), and
	 * clears the automatic gating (bit 28). The gate passes the rate of its
	 * parent.
	 */
	struct Gate : Clock, private Genode::Mmio<0x4>
	{
		enum : uint32_t { MANUAL = 1u << 20, CG_VAL = 1u << 21, AUTO = 1u << 28 };

		struct Reg : Register<0x0, 32> { };

		Clock &_parent;

		Gate(Clocks &clocks, Name const &name, Clock &parent,
		     Byte_range_ptr const &range)
		:
			Clock(clocks, name), Mmio(range), _parent(parent)
		{ }

		void _enable() override
		{
			write<Reg>((read<Reg>() | MANUAL | CG_VAL) & ~AUTO);
		}

		void _disable() override
		{
			write<Reg>(read<Reg>() & ~(MANUAL | CG_VAL));
		}

		Rate rate() const override { return _parent.rate(); }
	};

	/*
	 * User multiplexer of a clock-management unit (PLL_CON0 register)
	 *
	 * Enabling selects the oscillator (bit 4 cleared), so that the rate does
	 * not depend on what the boot loader left in the top clock-management
	 * unit, and waits until the busy bit 16 is cleared.
	 */
	struct Mux : Clock, private Genode::Mmio<0x4>
	{
		struct Reg : Register<0x0, 32>
		{
			struct Sel  : Bitfield<4,  1> { };
			struct Busy : Bitfield<16, 1> { };
		};

		Mux(Clocks &clocks, Name const &name, Byte_range_ptr const &range)
		:
			Clock(clocks, name), Mmio(range)
		{ }

		void _enable() override
		{
			write<Reg::Sel>(0);
			for (unsigned i = 0; i < MAX_BUSY_POLLS; i++)
				if (!read<Reg::Busy>())
					break;
		}
	};

	/*
	 * Protocol of a universal serial interface (USI), selected in the
	 * system registers of its block: 1 = UART, 2 = SPI, 4 = I2C
	 */
	struct Usi_mode : Clock, private Genode::Mmio<0x4>
	{
		enum Mode : unsigned { UART = 1, SPI = 2, I2C = 4 };

		struct Reg : Register<0x0, 32> { struct Sw_conf : Bitfield<0, 3> { }; };

		Mode const _mode;

		Usi_mode(Clocks &clocks, Name const &name,
		         Byte_range_ptr const &range, Mode mode)
		:
			Clock(clocks, name), Mmio(range), _mode(mode)
		{ }

		void _enable() override { write<Reg::Sw_conf>(_mode); }
	};

	/*
	 * Bits of a system register, set to 'value' within 'mask' on enable
	 *
	 * The platform driver owns the system-register page, so a driver does
	 * not need a mapping of it. The bits are left alone on disable.
	 */
	struct Sysreg_bits : Clock, private Genode::Mmio<0x4>
	{
		struct Reg : Register<0x0, 32> { };

		uint32_t const _mask;
		uint32_t const _value;

		Sysreg_bits(Clocks &clocks, Name const &name,
		            Byte_range_ptr const &range, uint32_t mask, uint32_t value)
		:
			Clock(clocks, name), Mmio(range), _mask(mask), _value(value & mask)
		{ }

		void _enable() override
		{
			write<Reg>((read<Reg>() & ~_mask) | _value);
		}
	};

	enum : addr_t {
		CMU_TOP_BASE       = 0x1e080000,
		CMU_PERIC1_BASE    = 0x10c00000,
		CMU_SIZE           = 0x4000,
		SYSREG_PERIC1_BASE = 0x10c21000,
		SYSREG_HSI2_BASE   = 0x14420000,
		SYSREG_SIZE        = 0x1000,

		/* CMU_TOP: gates of the clocks that feed the PERIC1 block */
		TOP_PERIC1_IP_OFFSET  = 0x2110,
		TOP_PERIC1_NOC_OFFSET = 0x2114,

		/* CMU_PERIC1: USI0 user multiplexer, SPI0 IP and bus clock */
		USI0_MUX_OFFSET   = 0x0620,
		SPI0_IPCLK_OFFSET = 0x2030,
		SPI0_PCLK_OFFSET  = 0x2034,

		/* SYSREG_PERIC1: protocol of USI0 */
		USI0_SW_CONF_OFFSET = 0x0,

		/* SYSREG_HSI2: I/O coherency of the UFS host controller */
		UFS_IOCC_OFFSET = 0x710,
		UFS_IOCC_MASK   = 0x3,
	};

	Attached_mmio<0> _cmu_top       { _env, {(char *)CMU_TOP_BASE,       CMU_SIZE} };
	Attached_mmio<0> _cmu_peric1    { _env, {(char *)CMU_PERIC1_BASE,    CMU_SIZE} };
	Attached_mmio<0> _sysreg_peric1 { _env, {(char *)SYSREG_PERIC1_BASE, SYSREG_SIZE} };
	Attached_mmio<0> _sysreg_hsi2   { _env, {(char *)SYSREG_HSI2_BASE,   SYSREG_SIZE} };

	Gate _top_peric1_ip  { _clocks, "top-peric1-ip",  _osc_clk,
	                       _cmu_top.range_at(TOP_PERIC1_IP_OFFSET) };
	Gate _top_peric1_noc { _clocks, "top-peric1-noc", _osc_clk,
	                       _cmu_top.range_at(TOP_PERIC1_NOC_OFFSET) };

	/* USI0 in SPI mode (touchscreen) */
	Mux      _usi0_mux      { _clocks, "usi0-mux",
	                          _cmu_peric1.range_at(USI0_MUX_OFFSET) };
	Gate     _spi0_ipclk    { _clocks, "spi0-ipclk", _osc_clk,
	                          _cmu_peric1.range_at(SPI0_IPCLK_OFFSET) };
	Gate     _spi0_pclk     { _clocks, "spi0-pclk",  _osc_clk,
	                          _cmu_peric1.range_at(SPI0_PCLK_OFFSET) };
	Usi_mode _usi0_spi_mode { _clocks, "usi0-spi-mode",
	                          _sysreg_peric1.range_at(USI0_SW_CONF_OFFSET),
	                          Usi_mode::SPI };

	/*
	 * UFS host controller. Its clock is set up by the boot loader and stays
	 * untouched, the rate is the one Android reports for it. The vendor's
	 * driver derives the timing of the PHY from it. The coherency bits make
	 * the DMA of the controller coherent with the CPU caches, as the
	 * driver's 'dma-coherent' device-tree property expects.
	 */
	Fixed_clock _ufs_embd { _clocks, "ufs-embd", Clock::Rate { 177664000 } };

	Sysreg_bits _ufs_iocc { _clocks, "ufs-iocc",
	                        _sysreg_hsi2.range_at(UFS_IOCC_OFFSET),
	                        UFS_IOCC_MASK, UFS_IOCC_MASK };

	Cmu(Genode::Env &env, Clocks &clocks, Clock &osc_clk)
	:
		_env(env), _clocks(clocks), _osc_clk(osc_clk)
	{ }
};

#endif /* _SRC__DRIVER__PLATFORM__GS201__CMU_H_ */
