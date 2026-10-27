/*
 * \brief  Hardware keys of the Pixel 7 as input events
 * \author Ivan Loskutov
 * \date   2026-09-30
 *
 * The power and volume keys are GPIO lines, active low. Each key uses a
 * Pin_state session and an IRQ session of the pin driver gs201_pio, both
 * labeled with the name of the pin, which is declared as input with an
 * interrupt on both edges. On each interrupt, the level of the pin is read
 * and a change is submitted as press or release to the Event session.
 *
 * config
 * + key | name: KEY_POWER      | pin: power
 * + key | name: KEY_VOLUMEUP   | pin: vol_up
 * + key | name: KEY_VOLUMEDOWN | pin: vol_down
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/registry.h>
#include <event_session/connection.h>
#include <input/keycodes.h>
#include <irq_session/connection.h>
#include <pin_state_session/connection.h>

namespace Gs201_keys {

	using namespace Genode;

	struct Key;
	struct Main;
}


struct Gs201_keys::Key
{
	using Pin_name = String<64>;

	Env               &_env;
	Event::Connection &_event;
	Input::Keycode const _code;

	Pin_state::Connection _state;
	Irq_connection        _irq;

	bool _pressed = _state.state() == false;

	void _handle_irq()
	{
		_irq.ack_irq();

		bool const pressed = (_state.state() == false);
		if (pressed == _pressed)
			return;

		_pressed = pressed;

		_event.with_batch([&] (Event::Session_client::Batch &batch) {
			if (_pressed) batch.submit(Input::Press   { _code });
			else          batch.submit(Input::Release { _code });
		});
	}

	Signal_handler<Key> _irq_handler { _env.ep(), *this, &Key::_handle_irq };

	Key(Env &env, Event::Connection &event, Input::Keycode code,
	    Pin_name const &pin)
	:
		_env(env), _event(event), _code(code),
		_state(env, pin.string()), _irq(env, pin.string())
	{
		_irq.sigh(_irq_handler);
		_irq.ack_irq();
	}

	virtual ~Key() { }
};


struct Gs201_keys::Main
{
	Env &_env;

	Heap _heap { _env.ram(), _env.rm() };

	Attached_rom_dataspace _config { _env, "config" };

	Event::Connection _event { _env };

	Registry<Registered<Key>> _keys { };

	Main(Env &env) : _env(env)
	{
		_config.node().for_each_sub_node("key", [&] (Node const &node) {

			auto const name = node.attribute_value("name", Input::Key_name());
			auto const pin  = node.attribute_value("pin",  Key::Pin_name());

			Input::Keycode const code = Input::key_code(name);
			if (code == Input::KEY_UNKNOWN || !pin.valid()) {
				warning("invalid key: ", node);
				return;
			}

			new (_heap) Registered<Key>(_keys, _env, _event, code, pin);
		});
	}
};


void Component::construct(Genode::Env &env)
{
	static Gs201_keys::Main main(env);
}
