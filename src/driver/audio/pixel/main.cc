/*
 * \brief  DDE audio driver of the Pixel 7: AoC start, sound card, sessions
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * Two ROMs configure the driver: 'settings' holds what is fixed for a boot
 * (timeouts, firmware log, firmware sizes), 'config' is the mixer policy and
 * changes at run time (audio.cc).
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/rpc_client.h>
#include <pd_session/pd_session.h>
#include <region_map/client.h>
#include <rm_session/connection.h>
#include <util/reconstructible.h>
#include <lx_emul/init.h>
#include <lx_kit/env.h>
#include <lx_kit/init.h>

#include "pixel_audio.h"
#include "audio_main.h"

extern "C" void lx_emul_rom_firmware_size(char const *name, unsigned long size);

namespace Pixel_audio {
	using namespace Genode;
	struct Main;
}


struct pixel_audio_config pixel_audio_config;

/* used by the Linux side as soon as the kernel runs, set by the constructor */
static Pixel_audio::Main *_main_ptr;


/**
 * Client of core's system-control interface, which issues an SMC with the
 * registers x0..x7 of the state and returns x0..x3. Core hands out the
 * capability to components started with 'managing_system: yes'. All calls
 * are served by one entrypoint of core, so a standard call to Trusty and
 * its restart come from the same CPU, as Trusty requires.
 */
struct System_control : Genode::Rpc_client<Genode::Pd_session::System_control>
{
	using State = Genode::Pd_session::Managing_system_state;

	explicit System_control(Genode::Capability<Genode::Pd_session::System_control> cap)
	: Rpc_client<Genode::Pd_session::System_control>(cap) { }

	State system_control(State const &state) override {
		return call<Rpc_system_control>(state); }
};

static Genode::Constructible<System_control> _system_control;


extern "C" void pixel_audio_smc(unsigned long const a[8], unsigned long r[4])
{
	System_control::State in { };
	for (unsigned i = 0; i < 8; i++)
		in.r[i] = a[i];

	System_control::State const out = _system_control->system_control(in);

	for (unsigned i = 0; i < 4; i++)
		r[i] = out.r[i];
}


struct Pixel_audio::Main
{
	Env &_env;

	Attached_rom_dataspace _dtb_rom  { _env, "dtb" };
	Attached_rom_dataspace _settings { _env, "settings" };

	Rm_connection _rm { _env };

	Signal_handler<Main> _signal_handler {
		_env.ep(), *this, &Main::_handle_signal };

	void _handle_signal()
	{
		Lx_kit::env().scheduler.execute();
	}

	/**
	 * Read the settings once, the bring-up runs once per boot
	 */
	void _read_config()
	{
		Node const config = _settings.node();

		struct pixel_audio_config &c = pixel_audio_config;

		c.verbose           = config.attribute_value("verbose",           false);
		c.nop_poll_ms       = config.attribute_value("nop_poll_ms",       200u);
		c.online_timeout_ms = config.attribute_value("online_timeout_ms", 10000u);

		c.aoc_log         = config.attribute_value("aoc_log",         false);
		c.aoc_log_rate    = config.attribute_value("aoc_log_rate",    20u);
		c.aoc_log_burst   = config.attribute_value("aoc_log_burst",   300u);
		c.aoc_log_replies = config.attribute_value("aoc_log_replies", false);

		/* a burst of zero lines would never print anything */
		if (c.aoc_log_burst < 1)
			c.aoc_log_burst = 1;

		/* exact sizes of firmware images, ROM dataspaces are page-rounded */
		config.for_each_sub_node("firmware", [&] (Node const &node) {
			using Name = String<64>;
			Name          const name = node.attribute_value("name", Name());
			unsigned long const size = node.attribute_value("size", 0UL);
			if (name.valid() && size)
				lx_emul_rom_firmware_size(name.string(), size);
		});

	}

	Main(Env &env) : _env(env)
	{
		log("--- Pixel audio driver ---");

		Lx_kit::initialize(_env, _signal_handler);
		_env.exec_static_constructors();

		_read_config();

		Genode::Capability<Pd_session::System_control> const cap =
			_env.pd().system_control_cap(Affinity::Location());
		if (!cap.valid())
			error("no system-control capability, the driver needs "
			      "'managing_system: yes'");
		_system_control.construct(cap);

		/* the 'config' ROM of the sound card, consumed from stage "play" on */
		audio_init(_env);

		_main_ptr = this;

		lx_emul_start_kernel(_dtb_rom.local_addr<void>());
	}
};


extern "C" void *pixel_audio_unbacked_window(unsigned long size)
{
	using namespace Genode;

	if (!_main_ptr)
		return nullptr;

	/*
	 * A managed dataspace without any attachment: the range is reserved in
	 * the component's address space, no access to it can be resolved
	 */
	Capability<Region_map> const cap = _main_ptr->_rm.create(size);
	if (!cap.valid()) {
		error("pixel_audio: cannot create a ", size, "-byte region map");
		return nullptr;
	}

	Region_map_client window { cap };

	void *ptr = nullptr;

	_main_ptr->_env.rm().attach(window.dataspace(), {
		.size       = { },  .offset    = { },
		.use_at     = { },  .at        = { },
		.executable = { },  .writeable = true
	}).with_result(
		[&] (Local::Constrained_region_map::Attachment &a) {
			a.deallocate = false;
			ptr = a.ptr; },
		[&] (Local::Constrained_region_map::Error) {
			error("pixel_audio: cannot reserve a ", size, "-byte window"); });

	return ptr;
}


void Component::construct(Genode::Env &env)
{
	static Pixel_audio::Main main(env);
}
