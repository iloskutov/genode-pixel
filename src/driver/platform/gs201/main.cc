/*
 * \brief  Platform driver for the Google GS201 SoC (Tensor G2)
 * \author Stefan Kalkowski
 * \author Ivan Loskutov
 * \date   2026-02-08
 *
 * Derived from the following file of the Genode OS framework and extended
 * by the power domains and clocks of the GS201 SoC:
 *
 *   repos/os/src/driver/platform/main.cc
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU Affero General Public
 * License version 3, see the file LICENSE.AGPL.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include <base/component.h>

#include <cmu.h>
#include <pmu.h>
#include <acpm.h>
#include <rtc.h>
#include <common.h>

namespace Driver { struct Main; };

struct Driver::Main
{
	Env                  & _env;
	Attached_rom_dataspace _config_rom     { _env, "config"        };
	Common                 _common         { _env, _config_rom     };
	Signal_handler<Main>   _config_handler { _env.ep(), *this,
	                                         &Main::_handle_config };
	void _handle_config();

	Fixed_clock _osc_clk { _common.devices().clocks(), "oscclk",
	                       Clock::Rate { 26*1000*1000 } };

	Cmu  _cmu  { _env, _common.devices().clocks(), _osc_clk };
	Pmu  _pmu  { _env, _common.devices().powers() };
	Acpm _acpm { _env, _common.devices().powers() };

	/* RTC of the PMIC, served as Rtc with the config attribute 'rtc' */
	Constructible<Pmic_rtc> _rtc { };

	Main(Genode::Env & env) : _env(env)
	{
		_config_rom.sigh(_config_handler);
		_handle_config();
		_common.announce_service();
	}
};


void Driver::Main::_handle_config()
{
	_config_rom.update();
	_common.handle_config(_config_rom.node());

	if (_config_rom.node().attribute_value("rtc", false) && !_rtc.constructed())
		_rtc.construct(_env, _acpm);
}


void Component::construct(Genode::Env &env) { static Driver::Main main(env); }
