/*
 * \brief  USB gadget driver of the Pixel 7 (DWC3 in device mode, DDE)
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * Linux' DWC3 core and UDC/composite framework with the vendor's usbdrd
 * PHY and a slim Exynos glue (exynos_glue.c), serving one Usb_gadget
 * session per function (session.cc, gadget.c). Before the kernel starts,
 * the USB switch of the MAX77759 Type-C controller is closed and the cable
 * orientation read, both over I2C.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <i2c_session/connection.h>
#include <lx_emul/init.h>
#include <lx_kit/env.h>
#include <lx_kit/init.h>
#include <usb_gadget/type_c.h>

#include <session.h>
#include <type_c.h>

extern "C" void lx_user_handle_io(void);

namespace Usb_gadget_driver {

	using namespace Genode;

	struct Type_c;
	struct Main;
}


static Usb_gadget::Polarity _polarity = Usb_gadget::Polarity::NONE;


extern "C" int usb_gadget_cc_polarity(void)
{
	return _polarity == Usb_gadget::Polarity::CC2 ? 1 : 0;
}


/**
 * MAX77759 TCPC at 0x25 (the session's bus address): USB switch and CC status
 */
struct Usb_gadget_driver::Type_c
{
	enum : uint8_t { CC_STATUS = 0x1d, USBSW_CTRL = 0x93, USBSW_CONNECT = 0x09 };

	I2c::Connection _i2c;

	uint8_t _read(uint8_t reg)
	{
		using Message = I2c::Session::Message;

		I2c::Session::Transaction t {
			Message(Message::WRITE, reg),
			Message(Message::READ,  (uint8_t)0) };

		_i2c.transmit(t);
		return t.value(1).value(0);
	}

	void _write(uint8_t reg, uint8_t value)
	{
		using Message = I2c::Session::Message;

		I2c::Session::Transaction t { Message(Message::WRITE, reg, value) };
		_i2c.transmit(t);
	}

	Type_c(Env &env) : _i2c(env)
	{
		try {
			uint8_t const before = _read(USBSW_CTRL);
			_write(USBSW_CTRL, USBSW_CONNECT);
			uint8_t const after = _read(USBSW_CTRL);

			uint8_t const cc = _read(CC_STATUS);
			_polarity = Usb_gadget::polarity(cc);

			log("Type-C: USB switch ", Hex(before), " -> ", Hex(after),
			    ", CC_STATUS ", Hex(cc), ", orientation ",
			    _polarity == Usb_gadget::Polarity::CC1 ? "cc1" :
			    _polarity == Usb_gadget::Polarity::CC2 ? "cc2" : "unknown");

			/*
			 * Read once: the cable (it carries the serial console too) is
			 * plugged in when the phone boots Genode. Unplugged or flipped
			 * later, the USBDP PHY keeps its lanes.
			 */
			if (_polarity == Usb_gadget::Polarity::NONE)
				warning("Type-C: no cable orientation, SuperSpeed lanes of CC1 assumed");
		}
		catch (I2c::Session::Bus_error) {
			error("Type-C: I2C bus error, USB switch not closed");
		}
	}
};


struct Usb_gadget_driver::Main
{
	Env &_env;

	Attached_rom_dataspace _config_rom { _env, "config" };
	Attached_rom_dataspace _dtb_rom    { _env, "dtb" };

	Signal_handler<Main> _signal_handler { _env.ep(), *this, &Main::_handle_signal };

	Type_c _type_c { _env };

	void _handle_signal()
	{
		lx_user_handle_io();
		Lx_kit::env().scheduler.execute();
	}

	Main(Env &env) : _env(env)
	{
		log("--- USB gadget driver started ---");

		Lx_kit::initialize(_env, _signal_handler);
		_env.exec_static_constructors();

		usb_gadget_sessions_init(_env, Lx_kit::env().heap, _config_rom.node(),
		                         _signal_handler);

		lx_emul_start_kernel(_dtb_rom.local_addr<void>());
	}
};


void Component::construct(Genode::Env &env)
{
	static Usb_gadget_driver::Main main(env);
}
