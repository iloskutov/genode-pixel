/*
 * \brief  I2C driver for one Samsung HSI2C controller of the GS201
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * Provides the I2c session for the slaves of one bus. The controller is
 * polled, every wait is bounded (hsi2c.h). A session reaches the one
 * address of its policy, and only if that address is listed by an 'allow'
 * node:
 *
 *   config | bus_speed_hz: 400000
 *   + allow  | bus_address: 0x25
 *   + policy | label_prefix: exynos_usb_gadget | bus_address: 0x25
 *            | max_write: 2
 *
 * 'max_write' limits the length of a write message (1: only a register
 * pointer can be written, the slave's registers stay untouched).
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <i2c_session/i2c_session.h>
#include <os/session_policy.h>
#include <platform_session/device.h>
#include <root/component.h>
#include <timer_session/connection.h>

/* local includes */
#include <hsi2c.h>

namespace Gs201_i2c {

	using namespace Genode;

	struct Io;
	struct Bus;
	struct Session_component;
	struct Root;
	struct Main;

	using Master = Hsi2c::Master<Io>;
}


/*
 * Registers and time for Hsi2c::Master
 */
struct Gs201_i2c::Io
{
	enum { SIZE = 0x100 };

	Platform::Device::Mmio<SIZE> &_mmio;
	Timer::Connection            &_timer;

	uint32_t volatile &_reg(unsigned offset)
	{
		return *(_mmio.local_addr<uint32_t volatile>() + offset / 4);
	}

	uint32_t read(unsigned offset)              { return _reg(offset); }
	void     write(unsigned offset, uint32_t v) { _reg(offset) = v; }

	uint64_t now_us()
	{
		return _timer.curr_time().trunc_to_plain_us().value;
	}

	/*
	 * The timer connection is used through 'curr_time' (timeout framework),
	 * which rules out 'usleep' on the same connection ("attempt to usleep
	 * in timeout-framework mode", first device run). The waits of a
	 * transfer are short: spin on the clock.
	 */
	void delay_us(uint64_t us)
	{
		uint64_t const end = now_us() + us;
		while (now_us() < end) { }
	}
};


struct Gs201_i2c::Bus
{
	Env &_env;

	Platform::Connection _platform { _env };
	Platform::Device     _device   { _platform };

	Platform::Device::Mmio<Io::SIZE> _mmio { _device };

	/* blocking waits only, no signal handler on this session */
	Timer::Connection _timer { _env };

	Io _io { _mmio, _timer };

	/* rate of the IP clock as the platform driver reports it */
	static unsigned long _ip_clock_hz(Platform::Connection &platform)
	{
		unsigned long rate = 0;
		platform.with_node([&] (Node const &devices) {
			devices.with_optional_sub_node("device", [&] (Node const &device) {
				device.for_each_sub_node("clock", [&] (Node const &clock) {
					if (clock.attribute_value("name", String<32>()) == "ipclk_hsi2c")
						rate = clock.attribute_value("rate", 0UL); }); }); });
		return rate;
	}

	Master _master;

	bool _usable = false;

	/* one line per kind of failure in a row, not one per transfer */
	Hsi2c::Result _last_logged = Hsi2c::Result::OK;
	unsigned      _late_logged = 0;
	bool          _refusal_logged = false;

	void _log_failure(uint8_t address)
	{
		Master::Failure const &f = _master.failure();

		if (f.result == _last_logged)
			return;

		_last_logged = f.result;

		error(Hsi2c::name(f.result), " at address ", Hex(address), " (",
		      f.read ? "read" : "write", ", ", f.done, " bytes moved)"
		      " INT_STAT=",   Hex(f.int_status),
		      " FIFO_STAT=",  Hex(f.fifo_status),
		      " TRANS_STAT=", Hex(f.trans_status),
		      " CTL=",        Hex(f.ctl),
		      " CONF=",       Hex(f.conf),
		      " AUTO_CONF=",  Hex(f.auto_conf),
		      ", controller reset; further failures of this kind are not logged");
	}

	Bus(Env &env, Node const &config)
	:
		_env(env),
		_master(_io,
		        (Hsi2c::Word)config.attribute_value("ip_clock_hz", _ip_clock_hz(_platform)),
		        (Hsi2c::Word)config.attribute_value("bus_speed_hz", 400000UL))
	{
		_usable = _master.init();

		if (!_usable) {
			error("no timing for IP clock ",
			      config.attribute_value("ip_clock_hz", _ip_clock_hz(_platform)),
			      " Hz and bus speed ",
			      config.attribute_value("bus_speed_hz", 400000UL),
			      " Hz (standard and fast mode only), controller left untouched");
			return;
		}

		Hsi2c::Timing const &t = _master.timing();

		log("IP clock ", config.attribute_value("ip_clock_hz", _ip_clock_hz(_platform)),
		    " Hz, bus ", t.scl_hz, " Hz");
	}

	/**
	 * Run a transaction: all messages but the last end without a stop
	 * condition, so that the next one begins with a repeated start
	 *
	 * \throw I2c::Session::Bus_error
	 */
	void transmit(Hsi2c::Policy const &policy, I2c::Session::Transaction &t)
	{
		using Message = I2c::Session::Message;

		if (!_usable || t.count() == 0)
			throw I2c::Session::Bus_error();

		/* check the whole transaction before the first byte moves */
		bool ok = true;
		for (unsigned i = 0; i < t.count(); i++) {
			Message &m = t.value(i);
			ok &= policy.message_ok(m.type == Message::READ, m.count());
		}

		if (!ok) {
			if (!_refusal_logged)
				error("transaction for address ", Hex(policy.address),
				      " refused: a message is empty, too long or a write"
				      " beyond the session's 'max_write' (logged once)");
			_refusal_logged = true;
			throw I2c::Session::Bus_error();
		}

		for (unsigned i = 0; i < t.count(); i++) {

			Message &m = t.value(i);

			bool const read = (m.type == Message::READ),
			           stop = (i + 1 == t.count());

			uint8_t bytes[Hsi2c::MAX_MESSAGE_BYTES] { };

			unsigned const len = m.count();

			if (!read)
				for (unsigned n = 0; n < len; n++)
					bytes[n] = m.value(n);

			Hsi2c::Result const result =
				_master.message(policy.address, read, bytes, len, stop);

			if (result != Hsi2c::Result::OK) {
				_log_failure(policy.address);
				throw I2c::Session::Bus_error();
			}

			if (read)
				for (unsigned n = 0; n < len; n++)
					m.value(n) = bytes[n];
		}

		if (_last_logged != Hsi2c::Result::OK) {
			log("bus works again");
			_last_logged = Hsi2c::Result::OK;
		}

		if (_master.late_done() && !_late_logged) {
			_late_logged = _master.late_done();
			warning("a read ended without TRANSFER_DONE (data delivered,"
			        " logged once)");
		}
	}
};


struct Gs201_i2c::Session_component : Rpc_object<I2c::Session>
{
	Bus                &_bus;
	Hsi2c::Policy const _policy;

	Session_component(Bus &bus, Hsi2c::Policy const &policy)
	: _bus(bus), _policy(policy) { }

	void transmit(Transaction &t) override
	{
		/* a session whose address is not in the allow list reaches nothing */
		if (!_policy.permitted)
			throw Bus_error();

		_bus.transmit(_policy, t);
	}
};


struct Gs201_i2c::Root : Root_component<Session_component>
{
	Bus                    &_bus;
	Attached_rom_dataspace &_config;

	enum { MAX_ALLOWED = 8 };

	Create_result _create_session(char const *args) override
	{
		Session_label const label = label_from_args(args);

		unsigned long allowed[MAX_ALLOWED] { };
		unsigned      count = 0;

		_config.node().for_each_sub_node("allow", [&] (Node const &node) {
			if (count < MAX_ALLOWED && node.has_attribute("bus_address"))
				allowed[count++] = node.attribute_value("bus_address", 0UL); });

		return with_matching_policy(label, _config.node(),

			[&] (Node const &policy) -> Create_result {

				if (!policy.has_attribute("bus_address")) {
					error("policy for '", label, "' lacks 'bus_address'");
					return Create_error::DENIED;
				}

				Hsi2c::Policy const p(
					policy.attribute_value("bus_address", 0UL), allowed, count,
					policy.attribute_value("max_write",
					                       (unsigned)Hsi2c::MAX_MESSAGE_BYTES));

				if (p.permitted)
					log("session '", label, "': address ", Hex(p.address),
					    ", write messages up to ", p.max_write, " byte(s)");
				else
					error("session '", label, "': address ",
					      Hex(policy.attribute_value("bus_address", 0UL)),
					      " is not in the <allow> list, every transfer of"
					      " this session fails with a bus error");

				return _alloc_obj(_bus, p);
			},

			[&] () -> Create_result {
				error("no policy for session '", label, "'");
				return Create_error::DENIED;
			});
	}

	Root(Env &env, Allocator &md_alloc, Bus &bus, Attached_rom_dataspace &config)
	:
		Root_component<Session_component>(&env.ep().rpc_ep(), &md_alloc),
		_bus(bus), _config(config)
	{ }
};


struct Gs201_i2c::Main
{
	Env &_env;

	Attached_rom_dataspace _config { _env, "config" };

	Bus _bus { _env, _config.node() };

	Sliced_heap _sliced_heap { _env.ram(), _env.rm() };

	Root _root { _env, _sliced_heap, _bus, _config };

	Main(Env &env) : _env(env)
	{
		_env.parent().announce(_env.ep().manage(_root));
	}
};


void Component::construct(Genode::Env &env)
{
	static Gs201_i2c::Main main(env);
}
