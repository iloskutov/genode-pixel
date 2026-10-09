/*
 * \brief  GS201 PIO driver
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

/* Genode includes */
#include <util/list_model.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/registry.h>
#include <base/attached_rom_dataspace.h>
#include <os/session_policy.h>
#include <pin_state_session/component.h>
#include <pin_control_session/component.h>

/* local includes */
#include <pio.h>

namespace Pio_driver {

	struct Pin_declaration;
	struct Irq_handler;
	struct Main;
}


struct Pio_driver::Pin_declaration : List_model<Pin_declaration>::Element
{
	Name const name;

	static bool type_matches(Node const &node)
	{
		bool const valid_type = node.type() == "in"
		                     || node.type() == "out"
		                     || node.type() == "select";

		return valid_type && node.has_attribute("name");
	}

	bool matches(Node const &node) const
	{
		return type_matches(node)
		    && (name == Name::from_node(node))
		    && (attr.function.direction() == Function::from_node(node).direction());
	}

	Constructible<Pin_id> id { };

	Attr attr = Attr::disabled();

	unsigned ref_count = 0;

	Pin_declaration(Node const &node) : name(Name::from_node(node)) { }

	void update_from_node(Node const &pin, Pio &pio)
	{
		try {
			Pin_id const new_id = Pin_id::from_node(pin);

			attr = Attr::from_node(pin);

			bool const id_changed = (id.constructed() && *id != new_id);
			if (id_changed)
				pio.configure(*id, Attr::disabled());

			id.construct(new_id);

			bool const output = (attr.function.value == Function::OUTPUT);
			bool const on_demand_output = (output && attr.out_on_demand);

			if (!on_demand_output)
				pio.configure(new_id, attr);

			bool const configured_default_output = (output && !attr.out_on_demand);

			if (ref_count == 0 && configured_default_output)
				pio.state(new_id, attr.default_state);
		}
		catch (... ) {

			if (id.constructed())
				pio.configure(*id, Attr::disabled());

			id.destruct();
			attr = Attr::disabled();
		}
	}

	bool matches(Pin_id pin_id, Pin::Direction dir) const
	{
		return id.constructed() && (*id == pin_id) && (dir == attr.function.direction());
	}
};


/*
 * One GIC interrupt of a GPIO controller: a per-pin wakeup EINT (ALIVE,
 * FAR_ALIVE) or a controller's combined GPIO-group EINT. Either way the
 * pending bits of all IRQ pins are scanned (deliver_pin_irqs).
 */
struct Pio_driver::Irq_handler : Registry<Irq_handler>::Element
{
	struct Fn : Interface { virtual void handle_irq() = 0; };

	Platform::Device::Irq _irq;

	Fn &_fn;

	Signal_handler<Irq_handler> _handler;

	void _handle_irq()
	{
		_fn.handle_irq();
		_irq.ack();
	}

	Irq_handler(Registry<Irq_handler> &registry, Env &env,
	            Platform::Device &device, unsigned index, Fn &fn)
	:
		Registry<Irq_handler>::Element(registry, *this),
		_irq(device, Platform::Device::Irq::Index { index }), _fn(fn),
		_handler(env.ep(), *this, &Irq_handler::_handle_irq)
	{
		_irq.sigh(_handler);
	}
};


struct Pio_driver::Main : Pin::Driver<Pin_id>, Irq_handler::Fn
{
	Env &_env;

	Heap _heap { _env.ram(), _env.rm() };

	Platform::Connection _platform { _env };

	/* number of IRQs of a granted controller device, -1 if not granted */
	int _granted_irqs(char const *name)
	{
		int result = -1;
		_platform.with_node([&] (Node const &devices) {
			devices.for_each_sub_node("device", [&] (Node const &dev) {
				if (dev.attribute_value("name", Platform::Device::Name()) != name)
					return;
				result = 0;
				dev.for_each_sub_node("irq", [&] (Node const &) { result++; });
			});
		});
		return result;
	}

	Pio _pio { _platform, [&] (char const *name) { return _granted_irqs(name) >= 0; } };

	Registry<Irq_handler> _irq_handlers { };

	void _construct_irq_handlers()
	{
		for (unsigned c = 0; c < Controller::NUM; c++) {
			if (!_pio.present(Controller::Value(c)))
				continue;
			char const *name = Controller::device_name(Controller::Value(c));
			int const irqs = _granted_irqs(name);
			for (int i = 0; i < irqs; i++)
				new (_heap) Irq_handler(_irq_handlers, _env,
				                        _pio._ctrl[c]->device, unsigned(i), *this);
		}
	}

	/**
	 * Irq_handler::Fn interface
	 */
	void handle_irq() override { deliver_pin_irqs(); }

	Attached_rom_dataspace _config { _env, "config" };

	Signal_handler<Main> _config_handler {
		_env.ep(), *this, &Main::_handle_config };

	void _handle_config();

	Pin_state::Root<Pin_id>   _pin_state_root   { _env, _heap, *this };
	Pin_control::Root<Pin_id> _pin_control_root { _env, _heap, *this };
	Pin::Irq_root<Pin_id>     _irq_root         { _env, _heap, *this };

	List_model<Pin_declaration> _pins { };

	template <typename FN>
	void _with_pin_declaration(Pin_id id, Pin::Direction dir, FN const &fn)
	{
		_pins.for_each([&] (Pin_declaration &pin) {
			if (pin.matches(id, dir))
				fn(pin); });
	}

	bool pin_state(Pin_id id) const override
	{
		return _pio.state(id);
	}

	void pin_state(Pin_id id, Pin::Level level) override
	{
		_pio.state(id, level);
	}

	Pin_id assigned_pin(Session_label, Pin::Direction) const override;

	void acquire_pin(Pin_id id, Pin::Direction dir) override
	{
		_with_pin_declaration(id, dir, [&] (Pin_declaration &pin) {

			pin.ref_count++;

			if (pin.attr.output()) {
				if (pin.attr.out_on_demand)
					_pio.configure(id, pin.attr);
				else
					_pio.state(id, pin.attr.default_state);
			}
		});
	}

	void release_pin(Pin_id id, Pin::Direction direction) override
	{
		_with_pin_declaration(id, direction, [&] (Pin_declaration &pin) {

			if (--pin.ref_count > 0)
				return;

			if (pin.attr.output()) {
				if (pin.attr.out_on_demand)
					_pio.configure(id, Attr::disabled());
				else
					_pio.state(id, pin.attr.default_state);
			}

			if (pin.attr.irq())
				_pio.irq_enabled(id, false);
		});
	}

	void irq_enabled(Pin_id id, bool enabled) override
	{
		_pio.irq_enabled(id, enabled);
	}

	bool irq_pending(Pin_id id) const override
	{
		return _pio.irq_pending(id);
	}

	void ack_irq(Pin_id id) override
	{
		_pio.clear_irq_status(id);
	}

	Main(Env &env) : _env(env)
	{
		_construct_irq_handlers();
		_config.sigh(_config_handler);
		_handle_config();
	}
};


Pio_driver::Pin_id Pio_driver::Main::assigned_pin(Session_label label,
                                                   Pin::Direction dir) const
{
	Constructible<Pin_id> pin_id { };

	/*
	 * A Pin_state session may also read back the level of an <out> pin
	 * (e.g., a driver sensing the panel-reset line left high by the boot
	 * loader)
	 */
	auto matches = [&] (Pin_declaration const &pin) {
		Pin::Direction const pin_dir = pin.attr.function.direction();
		return pin_dir == dir
		    || (dir == Pin::Direction::IN && pin_dir == Pin::Direction::OUT); };

	with_matching_policy(label, _config.node(), [&] (Node const &policy) {
		Name const name { policy.attribute_value("pin", Name::String()) };
		_pins.for_each([&] (Pin_declaration const &pin) {
			if (pin.name == name && matches(pin))
				pin_id.construct(*pin.id); });
	}, [] { });

	if (!pin_id.constructed()) {
		char const *node_type = (dir == Pin::Direction::IN) ? "'in'" : "'out'";
		warning("missing ", node_type, " pin assignment for session '", label, "'");
		throw Service_denied();
	}

	return *pin_id;
}


void Pio_driver::Main::_handle_config()
{
	_config.update();

	auto create = [&] (Node const &node) -> Pin_declaration &
	{
		return *new (_heap) Pin_declaration(node);
	};

	auto destroy = [&] (Pin_declaration &pin)
	{
		if (pin.id.constructed())
			_pio.configure(*pin.id, Attr::disabled());

		Genode::destroy(_heap, &pin);
	};

	auto update = [&] (Pin_declaration &pin, Node const &node)
	{
		pin.update_from_node(node, _pio);
	};

	_pins.update_from_node(_config.node(), create, destroy, update);

	_pin_state_root  .update_assignments();
	_pin_control_root.update_assignments();
}


void Component::construct(Genode::Env &env)
{
	static Pio_driver::Main main(env);
}
