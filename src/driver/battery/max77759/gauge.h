/*
 * \brief  MAX77759 fuel gauge: register meanings and the monitor's rules
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * No Genode dependency, covered by the host tests in test/.
 *
 * The gauge is a ModelGauge m5 ("gauge type 2" of the reference driver,
 * google-modules/bms max1720x_battery.c, maxfg_common.h, max_m5_reg.h).
 * Registers are 16 bit, sent low byte first. Only the registers listed
 * here are read, none is written.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _GAUGE_H_
#define _GAUGE_H_

namespace Max77759_gauge {

	enum Reg : unsigned char {
		STATUS       = 0x00,   /* flags, see 'Status' */
		REP_CAP      = 0x05,   /* remaining capacity */
		REP_SOC      = 0x06,   /* state of charge, 1/256 % */
		TEMP         = 0x08,   /* signed, 1/256 degree C */
		VCELL        = 0x09,   /* 78.125 uV */
		CURRENT      = 0x0a,   /* signed, 1.5625 uV / Rsense */
		AVG_CURRENT  = 0x0b,   /* as CURRENT, averaged */
		DEV_NAME     = 0x21,   /* 0x6200 on the Pixel 7 */
		FULL_CAP_REP = 0x35,   /* capacity when full, as REP_CAP */
		TASK_PERIOD  = 0x3c,   /* decides the capacity unit */
	};

	/* the registers of one reading, in the order they are read */
	static constexpr Reg read_order[] = { STATUS, REP_SOC, VCELL, CURRENT,
	                                      AVG_CURRENT, TEMP, REP_CAP,
	                                      FULL_CAP_REP, TASK_PERIOD, DEV_NAME };
	enum { NUM_REGS = sizeof(read_order) / sizeof(read_order[0]) };

	/* STATUS bits (max_m5_reg.h) */
	enum Status : unsigned {
		STATUS_POR = 1u << 1,   /* gauge was reset: model and learning lost */
		STATUS_BST = 1u << 3,   /* battery absent */
	};

	enum {
		/* sense resistor in units of 10 uOhm (DT maxim,rsense-default) */
		RSENSE_DEFAULT = 500,

		TASK_PERIOD_351MS = 0x2d00,   /* capacity LSB doubled (max_m5.c) */

		/* charging: average current into the battery, with hysteresis */
		CHARGING_ON_MA  = 50,
		CHARGING_OFF_MA = 10,

		/* a cell voltage outside this range is not a reading */
		VOLTAGE_MIN_MV = 2000,
		VOLTAGE_MAX_MV = 5000,

		STALE_AFTER = 3,   /* failed periods */
	};

	struct Registers;
	struct Reading;
	struct Monitor;

	static inline int      signed16(unsigned v)   { return (int)(short)(v & 0xffff); }
	static inline unsigned percent(unsigned reg);
	static inline unsigned voltage_mv(unsigned reg);
	static inline int      current_ma(unsigned reg, unsigned rsense);
	static inline int      temp_c(unsigned reg);
	static inline unsigned capacity_mah(unsigned reg, unsigned rsense, unsigned task_period);
	static inline bool     next_charging(bool charging, int avg_current_ma);
}


/* REP_SOC: the integer part, as reg_to_percentage */
unsigned Max77759_gauge::percent(unsigned reg)
{
	unsigned const p = (reg & 0xffff) >> 8;
	return p > 100 ? 100 : p;
}


/* VCELL: 78.125 uV per LSB (reg_to_micro_volt) */
unsigned Max77759_gauge::voltage_mv(unsigned reg)
{
	return (unsigned)(((unsigned long long)(reg & 0xffff) * 78125) / 1000000);
}


/*
 * CURRENT, AVG_CURRENT: 1.5625 uV / Rsense per LSB (reg_to_micro_amp),
 * positive into the battery. With 5 mOhm one LSB is 312.5 uA.
 */
int Max77759_gauge::current_ma(unsigned reg, unsigned rsense)
{
	if (!rsense)
		return 0;

	long long const ua = (long long)signed16(reg) * 156250 / (long long)rsense;
	return (int)(ua / 1000);
}


/* TEMP: 1/256 degree C per LSB, signed (reg_to_deci_deg_cel) */
int Max77759_gauge::temp_c(unsigned reg)
{
	return signed16(reg) / 256;
}


/*
 * REP_CAP, FULL_CAP_REP: 5 uVh / Rsense per LSB (reg_to_micro_amp_h),
 * doubled with the 351 ms task period (max_m5_period2caplsb). With 5 mOhm
 * one LSB is 1 mAh or 2 mAh.
 */
unsigned Max77759_gauge::capacity_mah(unsigned reg, unsigned rsense, unsigned task_period)
{
	if (!rsense)
		return 0;

	unsigned long long const uah = (unsigned long long)(reg & 0xffff) * 500000 / rsense;
	unsigned           const lsb = (task_period == TASK_PERIOD_351MS) ? 2 : 1;

	return (unsigned)(uah * lsb / 1000);
}


bool Max77759_gauge::next_charging(bool charging, int avg_current_ma)
{
	if (avg_current_ma >= CHARGING_ON_MA)  return true;
	if (avg_current_ma <= CHARGING_OFF_MA) return false;
	return charging;
}


struct Max77759_gauge::Registers
{
	unsigned short value[NUM_REGS] { };

	unsigned get(Reg reg) const
	{
		for (unsigned i = 0; i < NUM_REGS; i++)
			if (read_order[i] == reg)
				return value[i];
		return 0;
	}

	void set(Reg reg, unsigned short v)
	{
		for (unsigned i = 0; i < NUM_REGS; i++)
			if (read_order[i] == reg)
				value[i] = v;
	}
};


struct Max77759_gauge::Reading
{
	bool     valid          = false;
	unsigned percent        = 0;
	unsigned voltage_mv     = 0;
	int      current_ma     = 0;
	int      avg_current_ma = 0;
	int      temp_c         = 0;
	unsigned capacity_mah   = 0;
	unsigned full_mah       = 0;
	bool     gauge_reset    = false;   /* STATUS_POR */
	bool     battery_absent = false;   /* STATUS_BST */

	Reading() { }

	Reading(Registers const &regs, unsigned rsense)
	:
		percent       (Max77759_gauge::percent(regs.get(REP_SOC))),
		voltage_mv    (Max77759_gauge::voltage_mv(regs.get(VCELL))),
		current_ma    (Max77759_gauge::current_ma(regs.get(CURRENT), rsense)),
		avg_current_ma(Max77759_gauge::current_ma(regs.get(AVG_CURRENT), rsense)),
		temp_c        (Max77759_gauge::temp_c(regs.get(TEMP))),
		capacity_mah  (capacity_mah_of(regs, REP_CAP, rsense)),
		full_mah      (capacity_mah_of(regs, FULL_CAP_REP, rsense)),
		gauge_reset   (regs.get(STATUS) & STATUS_POR),
		battery_absent(regs.get(STATUS) & STATUS_BST)
	{
		/*
		 * A bus that reads all ones or all zeros, or a gauge without a
		 * cell, gives no voltage in the range of a lithium cell
		 */
		valid = voltage_mv >= VOLTAGE_MIN_MV && voltage_mv <= VOLTAGE_MAX_MV;
	}

	static unsigned capacity_mah_of(Registers const &regs, Reg reg, unsigned rsense)
	{
		return Max77759_gauge::capacity_mah(regs.get(reg), rsense,
		                                    regs.get(TASK_PERIOD));
	}
};


/**
 * What the component does with the outcome of each period
 *
 * A period either delivers a valid reading or fails (bus error or an
 * implausible reading). The last good values stay in the report; after
 * STALE_AFTER failed periods in a row they are marked stale.
 */
struct Max77759_gauge::Monitor
{
	struct Action
	{
		bool report      = false;   /* (re)write the report */
		bool log_reading = false;   /* first reading, percent or charging changed */
		bool log_failure = false;   /* first failed period of a row */
		bool log_stale   = false;   /* the report was just marked stale */
		bool log_back    = false;   /* first good period after failures */
	};

	bool     have_reading = false;
	Reading  last { };
	bool     charging = false;
	bool     stale    = false;
	unsigned failures = 0;

	Action good(Reading const &reading)
	{
		Action action { };

		bool const now_charging = next_charging(have_reading ? charging : false,
		                                        reading.avg_current_ma);

		action.log_reading = !have_reading
		                  || reading.percent != last.percent
		                  || now_charging    != charging;

		action.log_back = failures > 0;

		/* the report carries voltage, current and temperature: every period */
		action.report = true;

		last         = reading;
		charging     = now_charging;
		have_reading = true;
		stale        = false;
		failures     = 0;

		return action;
	}

	Action failed()
	{
		Action action { };

		failures++;

		action.log_failure = (failures == 1);

		if (failures == STALE_AFTER && have_reading && !stale) {
			stale            = true;
			action.report    = true;
			action.log_stale = true;
		}
		return action;
	}
};

#endif /* _GAUGE_H_ */
