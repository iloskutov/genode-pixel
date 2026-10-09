/*
 * \brief  Framebuffer driver for the display of the Google Pixel 7
 * \author Norman Feske
 * \author Ivan Loskutov
 * \date   2026-09-30
 *
 * Derived from the framebuffer driver for the Allwinner A64 SoC of the
 * genode-allwinner repository.
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <cpu/cache.h>
#include <capture_session/connection.h>
#include <os/pixel_rgb888.h>
#include <util/reconstructible.h>
#include <lx_emul/fb.h>
#include <lx_emul/init.h>
#include <lx_kit/env.h>
#include <lx_kit/init.h>

namespace Framebuffer {
	using namespace Genode;
	struct Driver;
}


struct Framebuffer::Driver
{
	Env                  & env;
	Attached_rom_dataspace dtb_rom { env, "dtb" };
	Attached_rom_dataspace config  { env, "config" };

	/*
	 * config display="on|off" switches the panel via DPMS,
	 * brightness="1..100" (percent) sets the panel's backlight level
	 */
	bool     _display_on = true;
	unsigned _brightness = 0;   /* 0: as the boot loader left it */

	void _handle_config()
	{
		config.update();

		bool const on = config.node().attribute_value("display", String<8>("on")) != "off";
		if (on != _display_on) {
			_display_on = on;
			lx_emul_framebuffer_power(on);
		}

		unsigned const brightness = config.node().attribute_value("brightness", 0u);
		if (brightness >= 1 && brightness <= 100 && brightness != _brightness) {
			_brightness = brightness;
			lx_emul_framebuffer_brightness(brightness);
		}

		Lx_kit::env().scheduler.execute();
	}

	Signal_handler<Driver> _config_handler { env.ep(), *this, &Driver::_handle_config };

	class Fb
	{
		private:

			Capture::Connection         _capture;
			Capture::Area const         _size;
			Capture::Connection::Screen _captured_screen;
			void                      * _base;

			/*
			 * Non_copyable
			 */
			Fb(const Fb&);
			Fb & operator=(const Fb&);

		public:

			/**
			 * Paint captured pixels into the framebuffer
			 *
			 * The framebuffer is cached memory, read by DECON's DMA: the rows
			 * of the painted area are cleaned from the data cache.
			 *
			 * \return number of pixels painted
			 */
			size_t paint()
			{
				using Pixel = Capture::Pixel;
				Surface<Pixel> surface((Pixel*)_base, _size);
				Capture::Rect const rect = _captured_screen.apply_to_surface(surface);
				if (!rect.area.count())
					return 0;

				size_t const pitch = _size.w * sizeof(Pixel);
				cache_clean_invalidate_data((addr_t)_base + rect.y1() * pitch,
				                            rect.h() * pitch);
				return rect.area.count();
			}

			void capture_stopped() { _capture.capture_stopped(); }

			void wakeup_sigh(Signal_context_capability sigh) {
				_capture.wakeup_sigh(sigh); }

			Fb(Env & env, void * base, unsigned xres, unsigned yres)
			:
				_capture(env),
				_size{xres, yres},
				_captured_screen(_capture, env.rm(), { .px       = _size,
				                                       .mm       = { },
				                                       .viewport = { { }, _size },
				                                       .rotate   = { },
				                                       .flip     = { } }),
				_base(base) {}
	};

	Constructible<Fb> fb {};

	/*
	 * Capture as in the Intel display driver: the lx update task (lx_user.c)
	 * paints and commits in sequence, and stops capturing after a few rounds
	 * without changes; nitpicker's wakeup signal resumes it.
	 */
	void _handle_wakeup()
	{
		lx_emul_framebuffer_wakeup();
		Lx_kit::env().scheduler.execute();
	}

	Signal_handler<Driver> _wakeup_handler { env.ep(), *this, &Driver::_handle_wakeup };

	/**
	 * Paint captured changes, return true if the framebuffer changed
	 */
	bool capture(bool may_stop)
	{
		if (!fb.constructed())
			return false;

		if (fb->paint())
			return true;

		if (may_stop)
			fb->capture_stopped();

		return false;
	}

	Signal_handler<Driver> _signal_handler {
		env.ep(), *this, &Driver::_handle_signal };

	void _handle_signal()
	{
		Lx_kit::env().scheduler.execute();
	}

	Driver(Env & env) : env(env)
	{
		Lx_kit::initialize(env, _signal_handler);
		env.exec_static_constructors();
	}

	void start()
	{
		log("--- exynos-drm framebuffer driver started ---");

		lx_emul_start_kernel(dtb_rom.local_addr<void>());

		config.sigh(_config_handler);
		_handle_config();
	}
};


static Framebuffer::Driver & driver(Genode::Env & env)
{
	static Framebuffer::Driver driver(env);
	return driver;
}


/**
 * Can be called already as side-effect of `lx_emul_start_kernel`,
 * that's why the Driver object needs to be constructed already here.
 */
extern "C" void lx_emul_framebuffer_ready(void * base, unsigned long,
                                          unsigned xres, unsigned yres)
{
	Genode::Env & env = Lx_kit::env().env;
	Framebuffer::Driver &drv = driver(env);
	drv.fb.construct(env, base, xres, yres);
	drv.fb->wakeup_sigh(drv._wakeup_handler);
}


extern "C" int lx_emul_framebuffer_capture(int may_stop)
{
	return driver(Lx_kit::env().env).capture(may_stop);
}


void Component::construct(Genode::Env &env)
{
	driver(env).start();
}
