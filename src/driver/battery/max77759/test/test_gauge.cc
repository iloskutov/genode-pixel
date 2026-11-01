/*
 * \brief  Host test of the fuel-gauge conversions and the monitor's rules
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 *   make -C repos/pixel/src/driver/battery/max77759/test
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <cstdio>

#include <gauge.h>

using namespace Max77759_gauge;

static int failures = 0, checks = 0;

#define CHECK(c) do { checks++; if (!(c)) { \
	std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define CHECK_EQ(a, b) do { checks++; long long _a = (a); long long _b = (b); if (_a != _b) { \
	std::printf("FAIL %s:%d %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, \
	            _a, _b); failures++; } } while (0)

static Registers raw_of(unsigned soc, unsigned vcell, unsigned current, unsigned avg,
                  unsigned temp, unsigned status = 0)
{
	Registers raw;
	raw.set(REP_SOC,      (unsigned short)soc);
	raw.set(VCELL,        (unsigned short)vcell);
	raw.set(CURRENT,      (unsigned short)current);
	raw.set(AVG_CURRENT,  (unsigned short)avg);
	raw.set(TEMP,         (unsigned short)temp);
	raw.set(STATUS,       (unsigned short)status);
	raw.set(REP_CAP,      3800);
	raw.set(FULL_CAP_REP, 4355);
	raw.set(TASK_PERIOD,  0x1680);
	raw.set(DEV_NAME,     0x6200);
	return raw;
}

int main()
{
	/* state of charge: integer part of 1/256 %, capped */
	CHECK_EQ(percent(0x0000), 0);
	CHECK_EQ(percent(0x00ff), 0);
	CHECK_EQ(percent(0x5780), 87);
	CHECK_EQ(percent(0x57ff), 87);
	CHECK_EQ(percent(0x6400), 100);
	CHECK_EQ(percent(0x6580), 100);    /* the gauge can report above 100 */
	CHECK_EQ(percent(0xffff), 100);

	/* cell voltage: 78.125 uV */
	CHECK_EQ(voltage_mv(0), 0);
	CHECK_EQ(voltage_mv(0xd2a4), 4212);    /* 53924 * 78.125 uV */
	CHECK_EQ(voltage_mv(0xb400), 3600);
	CHECK_EQ(voltage_mv(0xffff), 5119);

	/* current: 312.5 uA per LSB at 5 mOhm, signed, positive = charging */
	CHECK_EQ(current_ma(0, 500), 0);
	CHECK_EQ(current_ma(3200, 500), 1000);
	CHECK_EQ(current_ma(0xfc18, 500), -312);            /* -1000 LSB */
	CHECK_EQ(current_ma((unsigned)-3200 & 0xffff, 500), -1000);
	CHECK_EQ(current_ma(0x7fff, 500), 10239);
	CHECK_EQ(current_ma(0x8000, 500), -10240);
	CHECK_EQ(current_ma(3200, 1000), 500);              /* 10 mOhm */
	CHECK_EQ(current_ma(3200, 0), 0);                   /* no division by zero */

	/* temperature: 1/256 degree C, signed */
	CHECK_EQ(temp_c(0x1d80), 29);
	CHECK_EQ(temp_c(0x0000), 0);
	CHECK_EQ(temp_c(0xf600), -10);
	CHECK_EQ(temp_c(0x7fff), 127);

	/* capacity: 1 mAh per LSB at 5 mOhm, 2 mAh with the 351 ms task period */
	CHECK_EQ(capacity_mah(4355, 500, 0x1680), 4355);
	CHECK_EQ(capacity_mah(2177, 500, 0x2d00), 4354);
	CHECK_EQ(capacity_mah(4355, 1000, 0x1680), 2177);
	CHECK_EQ(capacity_mah(4355, 0, 0x1680), 0);

	/* charging hysteresis on the average current */
	CHECK(!next_charging(false, 0));
	CHECK(!next_charging(false, 49));
	CHECK( next_charging(false, 50));
	CHECK( next_charging(true,  11));
	CHECK(!next_charging(true,  10));
	CHECK(!next_charging(true, -300));
	CHECK(!next_charging(false, 30));
	CHECK( next_charging(true,  30));

	/* the raw set keeps the read order and finds every register */
	{
		Registers raw;
		for (unsigned i = 0; i < NUM_REGS; i++)
			raw.value[i] = (unsigned short)(i + 1);
		for (unsigned i = 0; i < NUM_REGS; i++)
			CHECK_EQ(raw.get(read_order[i]), i + 1);
		CHECK_EQ(NUM_REGS, 10);
		CHECK_EQ(raw.get((Reg)0x18), 0);   /* a register that is not read */
	}

	/* a reading */
	{
		Reading const r(raw_of(0x5780, 0xd2a4, 0xfc18, 0xfc18, 0x1d80), RSENSE_DEFAULT);
		CHECK(r.valid);
		CHECK_EQ(r.percent, 87);
		CHECK_EQ(r.voltage_mv, 4212);
		CHECK_EQ(r.current_ma, -312);
		CHECK_EQ(r.temp_c, 29);
		CHECK_EQ(r.capacity_mah, 3800);
		CHECK_EQ(r.full_mah, 4355);
		CHECK(!r.gauge_reset && !r.battery_absent);
	}

	/* status flags */
	{
		Reading const r(raw_of(0x5780, 0xd2a4, 0, 0, 0x1d80, STATUS_POR | STATUS_BST),
		                RSENSE_DEFAULT);
		CHECK(r.gauge_reset && r.battery_absent);
	}

	/* what a dead bus looks like is not a reading */
	{
		Registers ones, zeros;
		for (unsigned i = 0; i < NUM_REGS; i++) ones.value[i] = 0xffff;
		CHECK(!Reading(ones,  RSENSE_DEFAULT).valid);
		CHECK(!Reading(zeros, RSENSE_DEFAULT).valid);
		CHECK(!Reading().valid);
		CHECK(!Reading(raw_of(0x5780, 0x6000, 0, 0, 0), RSENSE_DEFAULT).valid);  /* 1.9 V */
		CHECK( Reading(raw_of(0x0100, 0x9000, 0, 0, 0), RSENSE_DEFAULT).valid);  /* 2.88 V */
	}

	/* monitor: first reading is logged, then only changes */
	{
		Monitor m;
		Reading const a(raw_of(0x5780, 0xd2a4, 0xfc18, 0xfc18, 0x1d80), RSENSE_DEFAULT);

		Monitor::Action act = m.good(a);
		CHECK(act.report && act.log_reading && !act.log_back);
		CHECK(!m.charging && !m.stale && m.have_reading);

		act = m.good(a);
		CHECK(act.report && !act.log_reading);

		/* voltage moves, percent does not: no line */
		act = m.good(Reading(raw_of(0x5790, 0xd200, 0xfc00, 0xfc18, 0x1e00), RSENSE_DEFAULT));
		CHECK(act.report && !act.log_reading);

		/* percent drops */
		act = m.good(Reading(raw_of(0x56ff, 0xd200, 0xfc00, 0xfc18, 0x1e00), RSENSE_DEFAULT));
		CHECK(act.log_reading);
		CHECK_EQ(m.last.percent, 86);

		/* charger plugged: average current turns positive */
		act = m.good(Reading(raw_of(0x56ff, 0xd300, 3200, 3200, 0x1e00), RSENSE_DEFAULT));
		CHECK(act.log_reading && m.charging);

		/* inside the hysteresis band: stays, no line */
		act = m.good(Reading(raw_of(0x56ff, 0xd300, 64, 64, 0x1e00), RSENSE_DEFAULT));  /* 20 mA */
		CHECK(!act.log_reading && m.charging);

		/* unplugged */
		act = m.good(Reading(raw_of(0x56ff, 0xd200, 0xfc18, 0xfc18, 0x1e00), RSENSE_DEFAULT));
		CHECK(act.log_reading && !m.charging);
	}

	/* monitor: the first reading with a charger attached */
	{
		Monitor m;
		Monitor::Action const act =
			m.good(Reading(raw_of(0x3200, 0xc800, 3200, 3200, 0x1900), RSENSE_DEFAULT));
		CHECK(act.log_reading && m.charging);
	}

	/* monitor: failures, stale after three periods, recovery */
	{
		Monitor m;
		Reading const a(raw_of(0x5780, 0xd2a4, 0xfc18, 0xfc18, 0x1d80), RSENSE_DEFAULT);
		m.good(a);

		Monitor::Action act = m.failed();
		CHECK(act.log_failure && !act.report && !act.log_stale && !m.stale);
		act = m.failed();
		CHECK(!act.log_failure && !act.report && !m.stale);
		act = m.failed();
		CHECK(!act.log_failure && act.report && act.log_stale && m.stale);
		CHECK_EQ(m.last.percent, 87);      /* the last values are kept */
		act = m.failed();
		CHECK(!act.log_failure && !act.report && !act.log_stale && m.stale);

		act = m.good(a);
		CHECK(act.report && act.log_back && !m.stale);
		CHECK(!act.log_reading);           /* same percent, same charging */
		CHECK_EQ(m.failures, 0);

		/* a new row of failures is logged again */
		act = m.failed();
		CHECK(act.log_failure);
	}

	/* monitor: failures before any reading never produce a report */
	{
		Monitor m;
		for (unsigned i = 0; i < 10; i++) {
			Monitor::Action const act = m.failed();
			CHECK(!act.report && !act.log_stale);
			CHECK_EQ(act.log_failure, i == 0);
		}
		CHECK(!m.stale && !m.have_reading);
	}

	if (failures)
		std::printf("test_gauge: FAILED (%d of %d checks)\n", failures, checks);
	else
		std::printf("test_gauge: passed (%d checks)\n", checks);
	return failures ? 1 : 0;
}
