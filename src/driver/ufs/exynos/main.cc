/*
 * \brief  UFS storage driver for the Google Pixel 7
 * \author Ivan Loskutov
 * \date   2026-10-28
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <lx_emul/init.h>
#include <lx_emul/shared_dma_buffer.h>
#include <lx_kit/env.h>
#include <lx_kit/init.h>
#include <lx_user/io.h>
#include <genode_c_api/block.h>

namespace Ufs_driver {
	using namespace Genode;
	struct Main;
}


struct Ufs_driver::Main
{
	Env &_env;

	Attached_rom_dataspace _config_rom { _env, "config" };
	Attached_rom_dataspace _dtb_rom    { _env, "dtb" };

	Signal_handler<Main> _signal_handler {
		_env.ep(), *this, &Main::_handle_signal };

	Signal_handler<Main> _config_handler {
		_env.ep(), *this, &Main::_handle_config };

	void _handle_signal()
	{
		lx_user_handle_io();
		Lx_kit::env().scheduler.execute();
		genode_block_notify_peers();
	}

	void _handle_config()
	{
		_config_rom.update();
		genode_block_apply_config(_config_rom.node());
	}

	Main(Env &env) : _env(env)
	{
		Lx_kit::initialize(_env, _signal_handler);
		_env.exec_static_constructors();

		genode_block_init(genode_env_ptr(env),
		                  genode_allocator_ptr(Lx_kit::env().heap),
		                  genode_signal_handler_ptr(_signal_handler),
		                  lx_emul_shared_dma_buffer_allocate,
		                  lx_emul_shared_dma_buffer_free);

		_config_rom.sigh(_config_handler);
		_handle_config();

		lx_emul_start_kernel(_dtb_rom.local_addr<void>());
	}
};


void Component::construct(Genode::Env &env)
{
	static Ufs_driver::Main main(env);
}
