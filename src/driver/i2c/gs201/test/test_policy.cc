/*
 * \brief  Host test of the HSI2C session rules and register values
 * \author Ivan Loskutov
 * \date   2026-10-07
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <check.h>
#include <model.h>

using namespace Hsi2c;

int main()
{
	unsigned long const gauge_only[] { 0x36 };

	/* the gauge, limited to the register-pointer write */
	{
		Policy const p(0x36, gauge_only, 1, 1);
		CHECK(p.permitted);
		CHECK_EQ(p.address, 0x36);
		CHECK(p.message_ok(true, 2));
		CHECK(p.message_ok(true, 8));
		CHECK(p.message_ok(false, 1));
		CHECK(!p.message_ok(false, 2));   /* register write */
		CHECK(!p.message_ok(false, 3));
		CHECK(!p.message_ok(true, 0));
		CHECK(!p.message_ok(true, 9));
	}

	/* the charger is not in the list: nothing passes, not even a read */
	{
		Policy const p(0x66, gauge_only, 1, 8);
		CHECK(!p.permitted);
		CHECK(!p.message_ok(true, 1));
		CHECK(!p.message_ok(false, 1));
	}

	/* no list, no access */
	{
		Policy const p(0x36, nullptr, 0, 8);
		CHECK(!p.permitted);
		Policy const d;
		CHECK(!d.permitted);
		CHECK(!d.message_ok(true, 1));
	}

	/* reserved and out-of-range addresses, also when listed */
	{
		unsigned long const odd[] { 0x00, 0x03, 0x78, 0x136, 0x36 };
		CHECK(!Policy(0x00,  odd, 5, 8).permitted);
		CHECK(!Policy(0x03,  odd, 5, 8).permitted);
		CHECK(!Policy(0x78,  odd, 5, 8).permitted);
		CHECK(!Policy(0x136, odd, 5, 8).permitted);   /* would alias 0x36 */
		CHECK( Policy(0x36,  odd, 5, 8).permitted);
	}

	/* the write limit never exceeds a message */
	CHECK_EQ(Policy(0x36, gauge_only, 1, 100).max_write, 8u);
	CHECK(!Policy(0x36, gauge_only, 1, 0).message_ok(false, 1));

	/* register values */
	CHECK_EQ(fifo_ctl(1), 0x00010013u);
	CHECK_EQ(fifo_ctl(2), 0x00020023u);
	CHECK_EQ(fifo_ctl(8), 0x00080083u);
	CHECK_EQ(fifo_ctl(60), 0x00080083u);

	CHECK_EQ(addr_reg(0, 0x36), 0x0700d800u);
	CHECK_EQ(addr_reg(0xffffffff, 0x36), 0x07f0d800u);
	CHECK_EQ((addr_reg(0x12345678, 0x66) >> 10) & 0x3ff, 0x66u);

	CHECK_EQ(ctl_reg(CTL_MASTER, true),  CTL_MASTER | CTL_RXCHON);
	CHECK_EQ(ctl_reg(CTL_MASTER | CTL_RXCHON, false), CTL_MASTER | CTL_TXCHON);

	CHECK_EQ(auto_conf_reg(0, 1, false, false), 0x00000001u);
	CHECK_EQ(auto_conf_reg(0, 2, true,  true),  0x00030002u);
	CHECK_EQ(auto_conf_reg(0x8003ffff, 2, false, true), 0x00020002u);

	CHECK_EQ(message_timeout_us(2, 325000), 100000u);
	CHECK_EQ(message_timeout_us(8, 100), 2160000u);
	CHECK_EQ(message_timeout_us(8, 0), 100000u);

	CHECK(classify(0) == Result::OK);
	CHECK(classify(INT_TRANSFER_DONE) == Result::OK);
	CHECK(classify(INT_NODEV | INT_TRANSFER_DONE) == Result::NO_ACK);
	CHECK(classify(1u << 9) == Result::BUS_ERROR);
	CHECK(classify(1u << 8) == Result::BUS_ERROR);
	CHECK(classify(1u << 11) == Result::BUS_ERROR);

	return check_result("test_policy");
}
