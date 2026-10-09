/*
 * \brief  Types of the GS201 PIO driver
 * \author Norman Feske
 * \author Ivan Loskutov
 * \date   2026-02-08
 *
 * All GPIO controllers of the SoC and their banks, from the vendor table
 * (google-modules/soc/gs drivers/pinctrl/gs/pinctrl-gs201.c). Banks are
 * named as in Linux and the device tree ("gpa8", "gpp0", "gph1"); matching
 * is case-insensitive.
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

#ifndef _TYPES_H_
#define _TYPES_H_

#include <common_types.h>

namespace Pio_driver {

	using namespace Pin_driver;

	struct Controller;
	struct Bank_info;
	struct Bank;
	struct Pin_id;

	extern Bank_info const bank_table[];
	extern unsigned  const bank_count;
}


/*
 * GPIO controllers, each one platform device ("gpio_<name>" in the board's
 * devices file)
 */
struct Pio_driver::Controller
{
	enum Value { ALIVE, FAR_ALIVE, PERIC0, PERIC1, HSI1, HSI2, NUM } value;

	static char const *device_name(Value v)
	{
		switch (v) {
		case ALIVE:     return "gpio_alive";
		case FAR_ALIVE: return "gpio_far_alive";
		case PERIC0:    return "gpio_peric0";
		case PERIC1:    return "gpio_peric1";
		case HSI1:      return "gpio_hsi1";
		case HSI2:      return "gpio_hsi2";
		case NUM:       break;
		}
		return "";
	}
};


/*
 * One bank: register block at 'pctl_offset' (CON/DAT/PUD/DRV, 0x20 bytes),
 * EINT registers at 0x700/0x900/0xa00 + 'eint_offset' (CON/MASK/PEND) and
 * 0x800 + 'fltcon_offset' (FLTCON, one byte per pin). Wakeup banks (ALIVE,
 * FAR_ALIVE) have one GIC interrupt per pin, all others one combined
 * interrupt per controller.
 */
struct Pio_driver::Bank_info
{
	char const        *name;
	Controller::Value  controller;
	unsigned           pctl_offset;
	unsigned           nr_pins;
	unsigned           eint_offset;
	unsigned           fltcon_offset;
};


struct Pio_driver::Bank
{
	unsigned value;   /* index into 'bank_table' */

	class Invalid : Exception { };

	Bank_info const &info() const { return bank_table[value]; }

	static bool _same(char const *a, char const *b)
	{
		auto lower = [] (char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
		for (; *a && *b; a++, b++)
			if (lower(*a) != lower(*b)) return false;
		return *a == *b;
	}

	static Bank from_node(Node const &node)
	{
		using Str = String<8>;
		Str const name = node.attribute_value("bank", Str());

		for (unsigned i = 0; i < bank_count; i++)
			if (_same(bank_table[i].name, name.string()))
				return { i };

		warning("unknown PIO bank name '", name, "'");
		throw Invalid();
	};
};


struct Pio_driver::Pin_id
{
	Bank  bank;
	Index index;

	static Pin_id from_node(Node const &node)
	{
		Pin_id const id { Bank::from_node(node), Index::from_node(node) };
		if (id.index.value >= id.bank.info().nr_pins) {
			warning("pin index ", id.index.value, " out of range for bank ",
			        id.bank.info().name);
			throw Bank::Invalid();
		}
		return id;
	}

	bool operator == (Pin_id const &other) const
	{
		return other.bank.value  == bank.value
		    && other.index.value == index.value;
	}

	bool operator != (Pin_id const &other) const { return !(operator == (other)); }

	void print(Output &out) const
	{
		Genode::print(out, bank.info().name, "[", index.value, "]");
	}
};

#endif /* _TYPES_H_ */
