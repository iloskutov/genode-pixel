/*
 * \brief  Device interface of the GS201 PIO driver
 * \author Norman Feske
 * \author Ivan Loskutov
 * \date   2026-02-08
 *
 * Samsung GS201 GPIO, all controllers (ALIVE, FAR_ALIVE, PERIC0/1, HSI1/2).
 * Per bank (0x20 bytes at the bank's pctl offset):
 *   CON 0x00 (4 bits/pin: 0 input, 1 output, 2..0xe alt, 0xf EINT)
 *   DAT 0x04 (1 bit/pin), PUD 0x08 (4 bits/pin: 0 none, 1 down, 3 up),
 *   DRV 0x0c (4 bits/pin)
 * EINT (both wakeup and GPIO-group banks):
 *   CON    0x700 + eint_offset   (4 bits/pin: 0 low, 1 high, 2 falling,
 *                                  3 rising, 4 both edges)
 *   FLTCON 0x800 + fltcon_offset (1 byte/pin, bit 7 enables the filter)
 *   MASK   0x900 + eint_offset   (1 bit/pin, 1 = masked)
 *   PEND   0xa00 + eint_offset   (1 bit/pin, write 1 to clear)
 *
 * The register layout and the bank table are taken from the vendor's Linux
 * driver drivers/pinctrl/gs/pinctrl-gs201.c (google-modules/soc/gs).
 *
 * Derived from the pin driver for the Allwinner A64 SoC of the
 * genode-allwinner repository.
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU Affero General Public
 * License version 3, see the file LICENSE.AGPL.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef _PIO_H_
#define _PIO_H_

/* Genode includes */
#include <platform_session/device.h>
#include <util/reconstructible.h>
#include <cpu/mmio_access.h>

/* local includes */
#include <types.h>

namespace Pio_driver { struct Pio; }


/* vendor table pinctrl-gs201.c (name, controller, pctl, pins, eint, fltcon) */
Pio_driver::Bank_info const Pio_driver::bank_table[] = {
	{ "gpa0",  Controller::ALIVE,     0x000, 8, 0x00, 0x00 },
	{ "gpa1",  Controller::ALIVE,     0x020, 7, 0x04, 0x08 },
	{ "gpa2",  Controller::ALIVE,     0x040, 4, 0x08, 0x10 },
	{ "gpa3",  Controller::ALIVE,     0x060, 4, 0x0c, 0x14 },
	{ "gpa4",  Controller::ALIVE,     0x080, 4, 0x10, 0x18 },
	{ "gpa5",  Controller::ALIVE,     0x0a0, 7, 0x14, 0x1c },
	{ "gpa9",  Controller::ALIVE,     0x0c0, 8, 0x18, 0x24 },
	{ "gpa10", Controller::ALIVE,     0x0e0, 2, 0x1c, 0x2c },
	{ "gpa6",  Controller::FAR_ALIVE, 0x000, 8, 0x00, 0x00 },
	{ "gpa7",  Controller::FAR_ALIVE, 0x020, 4, 0x04, 0x08 },
	{ "gpa8",  Controller::FAR_ALIVE, 0x040, 8, 0x08, 0x0c },
	{ "gpa11", Controller::FAR_ALIVE, 0x060, 2, 0x0c, 0x14 },
	{ "gpp0",  Controller::PERIC0,    0x000, 5, 0x00, 0x00 },
	{ "gpp1",  Controller::PERIC0,    0x020, 4, 0x04, 0x08 },
	{ "gpp2",  Controller::PERIC0,    0x040, 4, 0x08, 0x0c },
	{ "gpp3",  Controller::PERIC0,    0x060, 2, 0x0c, 0x10 },
	{ "gpp4",  Controller::PERIC0,    0x080, 4, 0x10, 0x14 },
	{ "gpp5",  Controller::PERIC0,    0x0a0, 2, 0x14, 0x18 },
	{ "gpp6",  Controller::PERIC0,    0x0c0, 4, 0x18, 0x1c },
	{ "gpp7",  Controller::PERIC0,    0x0e0, 2, 0x1c, 0x20 },
	{ "gpp8",  Controller::PERIC0,    0x100, 4, 0x20, 0x24 },
	{ "gpp9",  Controller::PERIC0,    0x120, 2, 0x24, 0x28 },
	{ "gpp10", Controller::PERIC0,    0x140, 4, 0x28, 0x2c },
	{ "gpp11", Controller::PERIC0,    0x160, 2, 0x2c, 0x30 },
	{ "gpp12", Controller::PERIC0,    0x180, 4, 0x30, 0x34 },
	{ "gpp13", Controller::PERIC0,    0x1a0, 2, 0x34, 0x38 },
	{ "gpp14", Controller::PERIC0,    0x1c0, 4, 0x38, 0x3c },
	{ "gpp15", Controller::PERIC0,    0x1e0, 2, 0x3c, 0x40 },
	{ "gpp16", Controller::PERIC0,    0x200, 4, 0x40, 0x44 },
	{ "gpp17", Controller::PERIC0,    0x220, 2, 0x44, 0x48 },
	{ "gpp18", Controller::PERIC0,    0x240, 4, 0x48, 0x4c },
	{ "gpp19", Controller::PERIC0,    0x260, 5, 0x4c, 0x50 },
	{ "gpp20", Controller::PERIC1,    0x000, 8, 0x00, 0x00 },
	{ "gpp21", Controller::PERIC1,    0x020, 4, 0x04, 0x08 },
	{ "gpp22", Controller::PERIC1,    0x040, 2, 0x08, 0x0c },
	{ "gpp23", Controller::PERIC1,    0x060, 8, 0x0c, 0x10 },
	{ "gpp24", Controller::PERIC1,    0x080, 4, 0x10, 0x18 },
	{ "gpp25", Controller::PERIC1,    0x0a0, 4, 0x14, 0x1c },
	{ "gpp26", Controller::PERIC1,    0x0c0, 5, 0x18, 0x20 },
	{ "gpp27", Controller::PERIC1,    0x0e0, 4, 0x1c, 0x28 },
	{ "gph0",  Controller::HSI1,      0x000, 6, 0x00, 0x00 },
	{ "gph1",  Controller::HSI1,      0x020, 7, 0x04, 0x08 },
	{ "gph2",  Controller::HSI2,      0x000, 6, 0x00, 0x00 },
	{ "gph4",  Controller::HSI2,      0x020, 6, 0x04, 0x08 },
};

unsigned const Pio_driver::bank_count =
	sizeof(Pio_driver::bank_table) / sizeof(Pio_driver::bank_table[0]);


struct Pio_driver::Pio
{
	/* one platform device per granted GPIO controller */
	struct Controller_mmio
	{
		Platform::Device             device;
		Platform::Device::Mmio<0x1000> mmio;

		Controller_mmio(Platform::Connection &platform, char const *name)
		:
			device(platform, Platform::Device::Name(name)),
			mmio(device, Platform::Device::Mmio<0x1000>::Index { 0 })
		{ }

		addr_t _reg_addr(unsigned off)
		{
			return (addr_t)mmio.range_at(off).start;
		}

		uint32_t read(unsigned off)
		{
			return Hw::mmio_read<uint32_t>(_reg_addr(off));
		}

		void write(unsigned off, uint32_t value)
		{
			Hw::mmio_write<uint32_t>(_reg_addr(off), value);
		}
	};

	Constructible<Controller_mmio> _ctrl[Controller::NUM];

	bool present(Controller::Value c) const { return _ctrl[c].constructed(); }

	Controller_mmio &_controller(Pin_id id)
	{
		Controller::Value const c = id.bank.info().controller;
		if (!_ctrl[c].constructed()) {
			error("PIO controller ", Controller::device_name(c),
			      " not granted, cannot access ", id);
			throw Bank::Invalid();
		}
		return *_ctrl[c];
	}

	/* field access: 'width' bits per pin in register 'off' */
	void _field(Pin_id id, unsigned off, unsigned width, unsigned value)
	{
		Controller_mmio &ctrl = _controller(id);
		unsigned const shift = id.index.value * width;
		uint32_t const mask  = ((1u << width) - 1) << shift;
		ctrl.write(off, (ctrl.read(off) & ~mask) | ((value << shift) & mask));
	}

	unsigned _field(Pin_id id, unsigned off, unsigned width)
	{
		unsigned const shift = id.index.value * width;
		return (_controller(id).read(off) >> shift) & ((1u << width) - 1);
	}

	unsigned _bank(Pin_id id) const { return id.bank.info().pctl_offset; }

	enum { CON = 0x0, DAT = 0x4, PUD = 0x8,
	       ECON = 0x700, EFLT = 0x800, EMASK = 0x900, EPEND = 0xa00,
	       CON_EINT = 0xf };

	static unsigned _pud(Pull::Value pull)
	{
		switch (pull) {
		case Pull::UP:      return 3;
		case Pull::DOWN:    return 1;
		case Pull::DISABLE: return 0;
		}
		return 0;
	}

	/* Pin_driver::Function (allwinner numbering) -> gs201 CON */
	static unsigned _con(Function f)
	{
		switch (f.value) {
		case Function::INPUT:   return 0;
		case Function::OUTPUT:  return 1;
		case Function::IRQ:     return CON_EINT;
		case Function::DISABLE: return 0;   /* input, high impedance */
		default:                return f.value;
		}
	}

	/* Irq_trigger -> EINT CON */
	static unsigned _econ(Irq_trigger t)
	{
		switch (t.value) {
		case Irq_trigger::LOW:     return 0;
		case Irq_trigger::HIGH:    return 1;
		case Irq_trigger::FALLING: return 2;
		case Irq_trigger::RISING:  return 3;
		case Irq_trigger::EDGES:   return 4;
		}
		return 3;
	}

	void _irq_mask(Pin_id id, bool masked)
	{
		_field(id, EMASK + id.bank.info().eint_offset, 1, masked);
	}

	Pio(Platform::Connection &platform, auto const &granted)
	{
		for (unsigned c = 0; c < Controller::NUM; c++) {
			char const *name = Controller::device_name(Controller::Value(c));
			if (granted(name))
				_ctrl[c].construct(platform, name);
		}
	}

	void configure(Pin_id id, Attr const &attr)
	{
		if (attr.irq()) {
			Bank_info const &b = id.bank.info();

			_irq_mask(id, true);
			_field(id, ECON + b.eint_offset, 4, _econ(attr.irq_trigger));

			/* FLTCON: one byte per pin, 4 pins per register */
			unsigned const flt = EFLT + b.fltcon_offset + (id.index.value / 4) * 4;
			Pin_id const byte { id.bank, Index { id.index.value % 4 } };
			_field(byte, flt, 8, 0x80);

			_field(id, _bank(id) + PUD, 4, _pud(attr.pull.value));
			_field(id, _bank(id) + CON, 4, CON_EINT);
			clear_irq_status(id);
			return;
		}
		_field(id, _bank(id) + CON, 4, _con(attr.function));
		_field(id, _bank(id) + PUD, 4, _pud(attr.pull.value));
	}

	bool state(Pin_id id) const
	{
		return const_cast<Pio *>(this)->_field(id, _bank(id) + DAT, 1);
	}

	void state(Pin_id id, Pin::Level level)
	{
		if (level == Pin::Level::HIGH_IMPEDANCE) {
			_field(id, _bank(id) + CON, 4, 0);
		} else {
			_field(id, _bank(id) + DAT, 1, level == Pin::Level::HIGH);
			_field(id, _bank(id) + CON, 4, 1);
		}
	}

	void irq_enabled(Pin_id id, bool enabled) { _irq_mask(id, !enabled); }

	bool irq_pending(Pin_id id) const
	{
		return const_cast<Pio *>(this)->_field(id, EPEND + id.bank.info().eint_offset, 1);
	}

	void clear_irq_status(Pin_id id)
	{
		/* write-one-to-clear: only this pin's bit */
		_controller(id).write(EPEND + id.bank.info().eint_offset,
		                      1u << id.index.value);
	}
};

#endif /* _PIO_H_ */
