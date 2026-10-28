/*
 * \brief  Host test of the HSI2C timing calculation
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 *   make -C repos/pixel/src/driver/i2c/gs201/test
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <check.h>
#include <model.h>

using Hsi2c::Timing;

int main()
{
	/*
	 * The values Linux prints for this controller (references/dmesg.txt,
	 * "exynos5-hsi2c 10d60000.hsi2c: exynos5_i2c_set_timing IPCLK =
	 * 199680000 OP_CLK = 400000 DIV = 31 Timing FS1 = 0x1F0FF00 TIMING FS2
	 * = 0x30003E0 TIMING FS3 = 0x1F0000")
	 */
	{
		Timing const t(199680000, 400000);
		CHECK(t.valid);
		CHECK_EQ(t.div, 31u);
		CHECK_EQ(t.fs1(0x0100ff00), 0x01f0ff00u);
		CHECK_EQ(t.fs2(0x03000300), 0x030003e0u);
		CHECK_EQ(t.fs3(0x00000000), 0x001f0000u);
		CHECK_EQ(t.scl_hz, 390000u);
	}

	/* what the driver runs with: OSCCLK 26 MHz, 400 kHz asked for */
	{
		Timing const t(26000000, 400000);
		CHECK(t.valid);
		CHECK_EQ(t.div, 4u);
		CHECK_EQ(t.scl_high,   0xf0u);   /* 9*26/50 = 4 zero bits */
		CHECK_EQ(t.start_hold, 0xf8u);
		CHECK_EQ(t.scl_hz, 325000u);
		CHECK(t.scl_hz <= 400000);
	}

	/* standard mode from 26 MHz */
	{
		Timing const t(26000000, 100000);
		CHECK(t.valid);
		CHECK_EQ(t.div, 16u);
		CHECK_EQ(t.scl_high,   0xf8u);   /* 25*26/170 = 3 */
		CHECK_EQ(t.start_hold, 0xfcu);
		CHECK_EQ(t.scl_hz, 95588u);
	}

	/* only the fields are replaced */
	{
		Timing const t(26000000, 400000);
		CHECK_EQ(t.fs1(0xffffffff), 0xfff8ffffu);
		CHECK_EQ(t.fs2(0xffffffff), 0xfffffff0u);
		CHECK_EQ(t.fs3(0xffffffff), 0xff04ffffu);
	}

	/* the bus is never faster than asked for */
	for (uint32_t ip = 1000000; ip <= 200000000; ip += 777777)
		for (uint32_t hz : { 100000u, 400000u, 50000u, 380000u }) {
			Timing const t(ip, hz);
			if (t.valid) {
				CHECK(t.scl_hz <= hz || t.div == 0);
				CHECK(t.div <= 0xff);
				CHECK(t.scl_high <= 0xff && t.start_hold <= 0xff);
			}
		}

	/* refused instead of wrapped or guessed */
	CHECK(!Timing(0, 400000).valid);
	CHECK(!Timing(26000000, 0).valid);
	CHECK(!Timing(26000000, 1000000).valid);      /* fast mode plus: not done */
	CHECK(!Timing(200000000, 1000).valid);        /* divider above 8 bits */

	/* a huge clock over a small divider: the shift does not overflow */
	{
		Timing const t(4000000000u, 400000);
		CHECK(!t.valid);
		Timing const u(400000000u, 400000);
		CHECK(u.valid);
		CHECK_EQ(u.div, 62u);
	}

	return check_result("test_timing");
}
