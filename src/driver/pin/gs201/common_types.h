/*
 * \brief  Common pin driver types
 * \author Norman Feske
 * \author Ivan Loskutov
 * \date   2026-02-08
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

#ifndef _COMMON_TYPES_H_
#define _COMMON_TYPES_H_

/* Genode includes */
#include <base/node.h>
#include <base/log.h>

namespace Pin_driver {

	using namespace Genode;

	struct Name;
	struct Index;
	struct Function;
	struct Pull;
	struct Irq_trigger;
	struct Attr;
}


struct Pin_driver::Name
{
	using String = Genode::String<32>;

	String string;

	static Name from_node(Node const &node)
	{
		return { node.attribute_value("name", String()) };
	}

	bool operator == (Name const &other) const { return other.string == string; }
};


/**
 * Pin index within bank
 */
struct Pin_driver::Index
{
	unsigned value;

	class Invalid : Exception { };

	static Index from_node(Node const &node)
	{
		if (!node.has_attribute("index")) {
			warning("pin declarion lacks 'index' attribute: ", node);
			throw Invalid();
		}
		return Index { node.attribute_value("index", 0u) };
	}
};


/*
 * Pin function: the 4-bit GPIO CON value (0 input, 1 output, 2..14 the
 * alternate functions, 15 EINT), plus two driver states outside that range.
 * 'IRQ' is an <in irq=...> pin (CON EINT, served as IRQ session); a
 * <select function="15"/> is EINT as a hardware function without IRQ
 * session, e.g. the panel's TE line to DECON.
 */
struct Pin_driver::Function
{
	enum Value { INPUT = 0, OUTPUT = 1, FN_MIN = 2, EINT = 0xf,
	             IRQ = 0x10, DISABLE = 0x11 } value;

	Function() = delete;
	Function(Value value) : value(value) { }

	class Invalid : Exception { };

	static Function from_node(Node const &node)
	{
		if (node.has_type("in"))
			return node.has_attribute("irq") ? Function { IRQ }
			                                 : Function { INPUT };

		if (node.has_type("out"))
			return Function { OUTPUT };

		if (!node.has_type("select"))
			throw Invalid();

		unsigned const n = node.attribute_value("function", (unsigned)DISABLE);

		/* no function attribute: the pin is left as disabled input */
		if (n == DISABLE)
			return Function { DISABLE };

		if (n > EINT) {
			warning("function number out of range: ", node);
			throw Invalid();
		}

		if (n == INPUT) {
			warning("function number ", n, " reserved for <in> pins");
			throw Invalid();
		}

		if (n == OUTPUT) {
			warning("function number ", n, " reserved for <out> pins");
			throw Invalid();
		}

		return Function { (Value)n };
	};

	Pin::Direction direction() const
	{
		return (value == OUTPUT) ? Pin::Direction::OUT : Pin::Direction::IN;
	}

	void print(Output &out) const
	{
		switch (value)
		{
			case INPUT:
				Genode::print(out, "INPUT");
				break;
			case OUTPUT:
				Genode::print(out, "OUTPUT");
				break;
			case IRQ:
				Genode::print(out, "IRQ");
				break;
			case DISABLE:
				Genode::print(out, "DISABLE");
				break;
			default:
				Genode::print(out, (unsigned)value);
				break;
		}
	}
};


struct Pin_driver::Pull
{
	enum Value { DISABLE = 0, UP = 1, DOWN = 2 } value;

	class Invalid : Exception { };

	static Pull from_node(Node const &node)
	{
		if (!node.has_attribute("pull"))
			return Pull { DISABLE };

		auto const value = node.attribute_value("pull", String<10>());
		if (value == "up")
			return Pull { UP };

		if (value == "down")
			return Pull { DOWN };

		warning("invalid pull attribute value: ", node);
		throw Invalid();
	};
};


struct Pin_driver::Irq_trigger
{
	enum Value { RISING = 0, FALLING = 1, HIGH = 2, LOW = 3, EDGES = 4 } value;

	class Invalid : Exception { };

	static Irq_trigger from_node(Node const &node)
	{
		if (!node.has_attribute("irq"))
			return Irq_trigger { RISING };

		auto const value = node.attribute_value("irq", String<10>());

		if (value == "rising")  return Irq_trigger { RISING };
		if (value == "falling") return Irq_trigger { FALLING };
		if (value == "high")    return Irq_trigger { HIGH };
		if (value == "low")     return Irq_trigger { LOW };
		if (value == "edges")   return Irq_trigger { EDGES };

		warning("invalid irq attribute value: ", node);
		throw Invalid();
	};
};


/**
 * Pin attributes
 */
struct Pin_driver::Attr
{
	Pull        pull;
	Function    function;
	Irq_trigger irq_trigger;
	bool        out_on_demand;  /* activate output on access by 'Pin_control' client */
	Pin::Level  default_state;

	bool output() const { return function.value == Function::OUTPUT; }
	bool irq()    const { return function.value == Function::IRQ; }

	static Attr from_node(Node const &node)
	{
		auto default_state_from_node = [] (Node const &node)
		{
			if (!node.has_attribute("default"))
				return Pin::Level::HIGH_IMPEDANCE;

			return node.attribute_value("default", false)
			       ? Pin::Level::HIGH : Pin::Level::LOW;
		};

		return { Pull::from_node(node),
		         Function::from_node(node),
		         Irq_trigger::from_node(node),
		         !node.has_attribute("default"),
		         default_state_from_node(node) };
	}

	static Attr disabled()
	{
		return { { Pull::DISABLE },
		         { Function::DISABLE },
		         { Irq_trigger::RISING },
		         false,
		         Pin::Level::HIGH_IMPEDANCE };
	}

	private:

		Attr(Pull pull, Function function, Irq_trigger irq_trigger,
		     bool out_on_demand, Pin::Level default_state)
		:
			pull(pull), function(function), irq_trigger(irq_trigger),
			out_on_demand(out_on_demand), default_state(default_state)
		{ }
};

#endif /* _COMMON_TYPES_H_ */
