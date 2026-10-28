/*
 * \brief  Host test of the polled HSI2C transfer against a controller model
 * \author Ivan Loskutov
 * \date   2026-10-07
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <check.h>
#include <model.h>

using namespace Hsi2c;

using Bus = Master<Model>;

/* read of a 16-bit register behind an 8-bit register pointer */
static Result read_reg(Bus &bus, uint8_t address, uint8_t reg, uint16_t &value)
{
	uint8_t pointer[1] { reg };
	Result r = bus.message(address, false, pointer, 1, false);
	if (r != Result::OK)
		return r;

	uint8_t data[2] { };
	r = bus.message(address, true, data, 2, true);
	value = (uint16_t)(data[0] | (data[1] << 8));
	return r;
}

int main()
{
	/* init: USI reset released, one software reset, timing, auto mode */
	{
		Model m;
		Bus bus(m, 26000000, 400000);

		Bus::Previous const before = bus.previous();
		CHECK_EQ(before.usi_con, 1u);
		CHECK_EQ(before.fs1, 0x0100ff00u);

		CHECK(bus.init());
		CHECK_EQ(m.reg[USI_CON / 4], 0u);
		CHECK_EQ(m.resets, 1u);
		CHECK_EQ(m.reg[CTL / 4], (uint32_t)CTL_MASTER);
		CHECK(m.reg[CONF / 4] & CONF_AUTO_MODE);
		CHECK_EQ(m.reg[TIMING_FS1 / 4], 0x01f8ff00u);
		CHECK_EQ(m.reg[TIMING_FS2 / 4], 0x030003f0u);
		CHECK_EQ(m.reg[TIMING_FS3 / 4], 0x00040000u);
		CHECK_EQ(m.reg[INT_ENABLE / 4], 0u);
		CHECK(m.bus.empty());
	}

	/* no usable timing: the controller is not touched */
	{
		Model m;
		Bus bus(m, 0, 400000);
		CHECK(!bus.init());
		CHECK_EQ(m.writes, 0u);
		uint8_t b[1] { 0 };
		CHECK(bus.message(0x36, true, b, 1, true) == Result::INVALID);
		CHECK_EQ(m.writes, 0u);
	}

	/* register read: pointer write without stop, read with stop */
	{
		Model m;
		m.slave_reg[0x06] = 0x5780;   /* RepSOC 87.5 % */
		m.slave_reg[0x09] = 0xd2a4;
		Bus bus(m, 26000000, 400000);
		CHECK(bus.init());

		uint16_t v = 0;
		CHECK(read_reg(bus, 0x36, 0x06, v) == Result::OK);
		CHECK_EQ(v, 0x5780);
		CHECK(read_reg(bus, 0x36, 0x09, v) == Result::OK);
		CHECK_EQ(v, 0xd2a4);

		CHECK_EQ(m.bus.size(), 4u);
		CHECK_EQ(m.bus[0].address, 0x36);
		CHECK(!m.bus[0].read && !m.bus[0].stop);
		CHECK_EQ(m.bus[0].bytes.size(), 1u);
		CHECK_EQ(m.bus[0].bytes[0], 0x06);
		CHECK(m.bus[1].read && m.bus[1].stop);
		CHECK_EQ(m.bus[1].bytes.size(), 2u);

		/* the only bytes ever written are the two register pointers */
		CHECK_EQ(m.written.size(), 2u);
		CHECK_EQ(m.written[0], 0x06);
		CHECK_EQ(m.written[1], 0x09);

		/* left quiet: nothing enabled, nothing pending, no reset */
		CHECK_EQ(m.reg[INT_ENABLE / 4], 0u);
		CHECK_EQ(m.reg[INT_STATUS / 4], 0u);
		CHECK_EQ(m.resets, 1u);
		CHECK_EQ(bus.late_done(), 0u);
		CHECK(bus.failure().result == Result::OK);
	}

	/* a full-length read */
	{
		Model m;
		for (unsigned i = 0; i < 4; i++)
			m.slave_reg[0x10 + i] = (uint16_t)(0x1100 * (i + 1) + i);
		Bus bus(m, 26000000, 400000);
		CHECK(bus.init());

		uint8_t pointer[1] { 0x10 }, data[8] { };
		CHECK(bus.message(0x36, false, pointer, 1, false) == Result::OK);
		CHECK(bus.message(0x36, true, data, 8, true) == Result::OK);
		CHECK_EQ(data[0], 0x00); CHECK_EQ(data[1], 0x11);
		CHECK_EQ(data[6], 0x03); CHECK_EQ(data[7], 0x44);
	}

	/* nobody at the address: no ACK, controller reset, usable afterwards */
	{
		Model m;
		m.slave_reg[0x06] = 0x6400;
		Bus bus(m, 26000000, 400000);
		CHECK(bus.init());

		uint16_t v = 0;
		CHECK(read_reg(bus, 0x55, 0x06, v) == Result::NO_ACK);
		CHECK(bus.failure().result == Result::NO_ACK);
		CHECK(bus.failure().int_status & INT_NODEV);
		CHECK(!bus.failure().read);
		CHECK_EQ(m.resets, 2u);
		CHECK_EQ(m.bus.size(), 1u);             /* the read was not attempted */
		CHECK(m.written.empty());
		CHECK_EQ(m.reg[INT_ENABLE / 4], 0u);
		CHECK_EQ(m.reg[TIMING_FS3 / 4], 0x00040000u);   /* timing is back */
		CHECK(m.reg[CONF / 4] & CONF_AUTO_MODE);

		CHECK(read_reg(bus, 0x36, 0x06, v) == Result::OK);
		CHECK_EQ(v, 0x6400);
	}

	/* an error other than a missing ACK */
	{
		Model m;
		m.lost = true;
		Bus bus(m, 26000000, 400000);
		CHECK(bus.init());
		uint8_t b[1] { 0 };
		CHECK(bus.message(0x36, false, b, 1, false) == Result::BUS_ERROR);
		CHECK_EQ(m.resets, 2u);
	}

	/* the controller does nothing (e.g. no clock): bounded, then timeout */
	{
		Model m;
		m.stuck = true;
		Bus bus(m, 26000000, 400000);
		CHECK(bus.init());

		uint64_t const t0 = m.time_us;
		uint8_t b[2] { 0x06, 0 };
		CHECK(bus.message(0x36, false, b, 1, false) == Result::TIMEOUT);
		uint64_t const write_us = m.time_us - t0;
		CHECK(write_us >= 100000 && write_us < 110000);
		CHECK(bus.failure().result == Result::TIMEOUT);
		CHECK_EQ(m.resets, 2u);

		uint64_t const t1 = m.time_us;
		CHECK(bus.message(0x36, true, b, 2, true) == Result::TIMEOUT);
		uint64_t const read_us = m.time_us - t1;
		CHECK(read_us >= 100000 && read_us < 110000);
		CHECK_EQ(bus.failure().done, 0u);
		CHECK(bus.failure().read);
		CHECK_EQ(m.resets, 3u);

		/* it polls, it does not spin: a bounded number of register reads */
		CHECK(m.reads < 20000);
	}

	/* held in USI reset (init skipped): timeout as well */
	{
		Model m;
		Bus bus(m, 26000000, 400000);
		uint8_t b[1] { 0 };
		CHECK(bus.message(0x36, false, b, 1, false) == Result::TIMEOUT);
	}

	/* a write that never reports its end: timeout, not success */
	{
		Model m;
		m.no_done = true;
		Bus bus(m, 26000000, 400000);
		CHECK(bus.init());
		uint8_t b[1] { 0x06 };
		CHECK(bus.message(0x36, false, b, 1, false) == Result::TIMEOUT);
	}

	/* a read whose bytes arrive without TRANSFER_DONE: data, counted */
	{
		Model m;
		m.slave_reg[0x06] = 0x1234;
		Bus bus(m, 26000000, 400000);
		CHECK(bus.init());
		uint8_t b[1] { 0x06 };
		CHECK(bus.message(0x36, false, b, 1, false) == Result::OK);
		m.no_done = true;
		uint8_t d[2] { };
		uint64_t const t0 = m.time_us;
		CHECK(bus.message(0x36, true, d, 2, true) == Result::OK);
		CHECK_EQ(d[0], 0x34); CHECK_EQ(d[1], 0x12);
		CHECK_EQ(bus.late_done(), 1u);
		CHECK(m.time_us - t0 < 10000);
	}

	/* message bounds */
	{
		Model m;
		Bus bus(m, 26000000, 400000);
		CHECK(bus.init());
		uint8_t b[16] { };
		unsigned const writes = m.writes;
		CHECK(bus.message(0x36, true,  b, 0, true) == Result::INVALID);
		CHECK(bus.message(0x36, true,  b, 9, true) == Result::INVALID);
		CHECK(bus.message(0x36, false, nullptr, 1, true) == Result::INVALID);
		CHECK_EQ(m.writes, writes);
		CHECK(m.bus.empty());
	}

	return check_result("test_transfer");
}
