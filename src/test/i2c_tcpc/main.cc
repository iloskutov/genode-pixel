/*
 * \brief  Test of the I2C driver: identity of the Type-C controller
 * \author Ivan Loskutov
 * \date   2026-10-28
 *
 * Reads the vendor and product ID registers (TCPCI registers 0x00 to 0x03)
 * of the Type-C controller of the MAX77759 and checks the product ID of
 * the MAX77759. Only the register pointer is written.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/component.h>
#include <i2c_session/connection.h>

namespace Test {

	using namespace Genode;

	struct Main;
}


struct Test::Main
{
	enum : uint8_t { VENDOR_ID = 0x00, MAX77759_PRODUCT_ID = 0x59 };

	Env &_env;

	I2c::Connection _tcpc { _env };

	Main(Env &env) : _env(env)
	{
		using Message = I2c::Session::Message;

		I2c::Session::Transaction t {
			Message(Message::WRITE, VENDOR_ID),
			Message(Message::READ, (uint8_t)0, (uint8_t)0, (uint8_t)0, (uint8_t)0) };

		try { _tcpc.transmit(t); }
		catch (I2c::Session::Bus_error) {
			error("FAIL bus error");
			return;
		}

		Message &id = t.value(1);

		unsigned const vendor  = id.value(0) | id.value(1) << 8;
		unsigned const product = id.value(2) | id.value(3) << 8;

		log("vendor ID ", Hex(vendor), ", product ID ", Hex(product));

		if ((product & 0xff) == MAX77759_PRODUCT_ID)
			log("PASS");
		else
			error("FAIL product ID of the MAX77759 expected (", Hex(MAX77759_PRODUCT_ID), ")");
	}
};


void Component::construct(Genode::Env &env) { static Test::Main main(env); }
