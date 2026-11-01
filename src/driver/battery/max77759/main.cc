/*
 * \brief  Battery state from the MAX77759 fuel gauge (read only)
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * Reads the gauge through an I2c session every 'period_ms' and writes the
 * report 'battery':
 *
 *   battery | percent: 87 | voltage_mv: 4212 | current_ma: -312
 *           | charging: no | temp_c: 29
 *
 * 'current_ma' is positive while current flows into the battery. After
 * three failed periods in a row the last values get 'stale: yes'.
 *
 * The component never writes a register of the gauge: every access is a
 * transaction of a one-byte write (the register pointer) and a two-byte
 * read. The registers and their units are in gauge.h.
 *
 * Config: config | period_ms: 5000 | rsense_uohm: 5000
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <i2c_session/connection.h>
#include <os/reporter.h>
#include <timer_session/connection.h>

/* local includes */
#include <gauge.h>

namespace Max77759_battery {

	using namespace Genode;
	using namespace Max77759_gauge;

	struct Main;
}


struct Max77759_battery::Main
{
	Env &_env;

	Attached_rom_dataspace _config { _env, "config" };

	uint64_t const _period_ms =
		max(_config.node().attribute_value("period_ms", (uint64_t)5000), (uint64_t)100);

	/* the gauge's unit of the sense resistor is 10 uOhm */
	unsigned const _rsense =
		_config.node().attribute_value("rsense_uohm", RSENSE_DEFAULT * 10U) / 10;

	I2c::Connection   _i2c   { _env };
	Timer::Connection _timer { _env };

	Expanding_reporter _reporter { _env, "battery", "battery" };

	Monitor _monitor { };

	/**
	 * Read one register: pointer write, repeated start, two bytes
	 *
	 * \throw I2c::Session::Bus_error
	 */
	uint16_t _read(Reg reg)
	{
		using Message = I2c::Session::Message;

		I2c::Session::Transaction t {
			Message(Message::WRITE, (uint8_t)reg),
			Message(Message::READ,  (uint8_t)0, (uint8_t)0) };

		_i2c.transmit(t);

		Message &data = t.value(1);

		return (uint16_t)(data.value(0) | (data.value(1) << 8));
	}

	bool _read_all(Registers &regs)
	{
		try {
			for (unsigned i = 0; i < NUM_REGS; i++)
				regs.value[i] = _read(read_order[i]);
			return true;
		}
		catch (I2c::Session::Bus_error) { }

		return false;
	}

	void _report()
	{
		Reading const &r = _monitor.last;

		_reporter.generate([&] (Generator &g) {
			g.attribute("percent",    r.percent);
			g.attribute("voltage_mv", r.voltage_mv);
			g.attribute("current_ma", r.current_ma);
			g.attribute("charging",   _monitor.charging ? "yes" : "no");
			g.attribute("temp_c",     r.temp_c);
			if (_monitor.stale)
				g.attribute("stale", "yes");
		});
	}

	void _log_reading(Registers const &regs)
	{
		Reading const &r = _monitor.last;

		log(r.percent, "% ", r.voltage_mv, " mV ", r.current_ma, " mA (avg ",
		    r.avg_current_ma, ") ", _monitor.charging ? "charging " : "", r.temp_c,
		    " C, ", r.capacity_mah, " of ", r.full_mah, " mAh"
		    " | raw Status=", Hex(regs.get(STATUS)),
		    " RepSOC=",       Hex(regs.get(REP_SOC)),
		    " VCell=",        Hex(regs.get(VCELL)),
		    " Current=",      Hex(regs.get(CURRENT)),
		    " AvgCurrent=",   Hex(regs.get(AVG_CURRENT)),
		    " Temp=",         Hex(regs.get(TEMP)),
		    " RepCap=",       Hex(regs.get(REP_CAP)),
		    " FullCapRep=",   Hex(regs.get(FULL_CAP_REP)),
		    " TaskPeriod=",   Hex(regs.get(TASK_PERIOD)),
		    " DevName=",      Hex(regs.get(DEV_NAME)));
	}

	bool _flags_logged = false;
	bool _periodic     = false;

	void _handle_period()
	{
		if (!_periodic) {
			_periodic = true;
			_timer.trigger_periodic(1000 * _period_ms);
		}

		Registers regs { };

		bool const read_ok = _read_all(regs);

		Reading const reading = read_ok ? Reading(regs, _rsense) : Reading();

		if (!reading.valid) {

			Monitor::Action const action = _monitor.failed();

			if (action.log_failure) {
				if (read_ok)
					error("implausible reading, ignored: VCell=", Hex(regs.get(VCELL)),
					      " RepSOC=", Hex(regs.get(REP_SOC)),
					      " Status=", Hex(regs.get(STATUS)),
					      " DevName=", Hex(regs.get(DEV_NAME)));
				else
					error("gauge not readable (bus error)");
			}
			if (action.log_stale)
				warning("no reading for ", (unsigned)STALE_AFTER,
				        " periods, the report is marked stale");
			if (action.report)
				_report();
			return;
		}

		Monitor::Action const action = _monitor.good(reading);

		if (action.log_back)
			log("gauge readable again");
		if (action.log_reading)
			_log_reading(regs);
		if (action.report)
			_report();

		if (!_flags_logged && (reading.gauge_reset || reading.battery_absent)) {
			_flags_logged = true;
			warning("gauge status ", Hex(regs.get(STATUS)), ":",
			        reading.gauge_reset    ? " reset since its model was loaded"
			                                 " (values of the default model)" : "",
			        reading.battery_absent ? " battery absent" : "");
		}
	}

	Signal_handler<Main> _period_handler { _env.ep(), *this, &Main::_handle_period };

	Main(Env &env) : _env(env)
	{
		/*
		 * The first reading is taken one second after the start, not in the
		 * constructor: the I2C driver may still come up
		 */
		_timer.sigh(_period_handler);
		_timer.trigger_once(1000 * 1000);
	}
};


void Component::construct(Genode::Env &env)
{
	static Max77759_battery::Main main(env);
}
