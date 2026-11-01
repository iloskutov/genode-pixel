/*
 * \brief  Driver for the Mali-G710 of the Pixel 7: DDE port of panthor
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * Starts the Linux kernel emulation with the device tree of the GPU; the
 * initcalls register the panthor platform driver and the device of the
 * tree, which runs the driver's probe function (power-up, GPU_ID, MMU,
 * firmware load and boot, scheduler, DRM device). The firmware image is
 * requested as ROM "mali_csffw.bin" (firmware.cc).
 *
 * Once the probe function is done, the Gpu service is announced
 * (session.cc; protocol: include/gpu/panthor_session.h).
 *
 *   config | dtb: panthor-google_pixel7.dtb | test_ops: no
 *
 *   test_ops    serve the request type TEST_SLEEP
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/env.h>
#include <base/log.h>

/* lx emul/kit includes */
#include <lx_emul/init.h>
#include <lx_kit/env.h>
#include <lx_kit/init.h>

/* local includes */
#include "emul.h"
#include "session.h"

namespace Driver {

	using namespace Genode;

	struct Main;
}


struct Driver::Main
{
	Env &_env;

	Attached_rom_dataspace _config_rom { _env, "config" };

	using Dtb_name = String<64>;

	Dtb_name const _dtb_name {
		_config_rom.node().attribute_value("dtb", Dtb_name("dtb")) };

	Attached_rom_dataspace _dtb_rom { _env, _dtb_name.string() };

	bool _attribute(char const *name) const
	{
		return _config_rom.node().attribute_value(name, false);
	}

	bool const _test_ops = _attribute("test_ops");

	/* cached buffer objects on request of a client (WB_MMAP), default on */
	bool const _bo_cached =
		_config_rom.node().attribute_value("bo_cached", true);

	Signal_handler<Main> _signal_handler {
		_env.ep(), *this, &Main::_handle_signal };

	void _handle_signal()
	{
		Lx_kit::env().scheduler.execute();
	}

	bool test_ops()   const { return _test_ops;   }
	bool bo_cached()  const { return _bo_cached;  }

	Main(Env &env);
};


/* for the configuration queries of the kernel side, which starts in the constructor */
static Driver::Main *_main_ptr;


Driver::Main::Main(Env &env) : _env(env)
{
	_main_ptr = this;

	log("--- panthor GPU driver started ---");

	Lx_kit::initialize(_env, _signal_handler);
	_env.exec_static_constructors();

	Gpu::prepare();

	lx_emul_start_kernel(_dtb_rom.local_addr<void>());
}



extern "C" int emul_config_test_ops(void)
{
	return _main_ptr && _main_ptr->test_ops();
}


extern "C" int emul_config_bo_cached(void)
{
	return _main_ptr && _main_ptr->bo_cached();
}


void Component::construct(Genode::Env &env)
{
	static Driver::Main main(env);
}
