/*
 * \brief  Rtc service of the RTC in the S2MPG12 PMIC
 * \author Ivan Loskutov
 * \date   2026-09-30
 *
 * The RTC is a function of the PMIC behind the ACPM firmware, whose mailbox
 * this driver owns, so that a separate RTC driver could not map it. The
 * service is read-only: the time is latched by toggling RTC_UPDATE.RUDR, then
 * the seven counter registers are read in one ACPM bulk transfer. The register
 * layout is from the vendor's rtc-s2mpg12.c.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _SRC__DRIVER__PLATFORM__GS201__RTC_H_
#define _SRC__DRIVER__PLATFORM__GS201__RTC_H_

#include <base/heap.h>
#include <root/component.h>
#include <rtc_session/rtc_session.h>

#include <acpm.h>

namespace Driver { struct Pmic_rtc; }


struct Driver::Pmic_rtc
{
	enum {
		TYPE_RTC     = 0x02,   /* block of the RTC in the S2MPG12 */
		RTC_UPDATE   = 0x01,
		RTC_SEC      = 0x06,   /* SEC MIN HOUR WEEK DAY MON YEAR */
		NUM_COUNTERS = 7,
		RUDR         = 1u << 0,
	};

	struct Session_component : Rpc_object<Rtc::Session>
	{
		Pmic_rtc &_rtc;

		Session_component(Pmic_rtc &rtc) : _rtc(rtc) { }

		void set_sigh(Signal_context_capability) override { }

		Rtc::Timestamp current_time() override { return _rtc._current_time(); }
	};

	struct Root : Root_component<Session_component>
	{
		Pmic_rtc &_rtc;

		Create_result _create_session(char const *) override {
			return *new (md_alloc()) Session_component(_rtc); }

		Root(Env &env, Allocator &md_alloc, Pmic_rtc &rtc)
		:
			Root_component<Session_component>(&env.ep().rpc_ep(), &md_alloc),
			_rtc(rtc)
		{ }
	};

	Env  &_env;
	Acpm &_acpm;

	Sliced_heap _sliced_heap { _env.ram(), _env.rm() };
	Root        _root        { _env, _sliced_heap, *this };

	/* latch the counters into the readable shadow registers */
	bool _latch()
	{
		using A = Acpm;

		if (!_acpm._pmic_update(RTC_UPDATE, 0, RUDR, A::PMIC_CHANNEL, TYPE_RTC))
			return false;
		_acpm._timer.usleep(50);

		if (!_acpm._pmic_update(RTC_UPDATE, RUDR, RUDR, A::PMIC_CHANNEL, TYPE_RTC))
			return false;
		_acpm._timer.usleep(1000);
		return true;
	}

	Rtc::Timestamp _current_time()
	{
		uint8_t c[NUM_COUNTERS] { };

		if (!_latch() || !_acpm._pmic_bulk_read(TYPE_RTC, RTC_SEC, NUM_COUNTERS, c)) {
			error("RTC: reading the PMIC counters failed");
			return { };
		}

		/* c[3] is the one-hot weekday */
		return Rtc::Timestamp { .microsecond = 0,
		                        .second      = c[0] & 0x7fu,
		                        .minute      = c[1] & 0x7fu,
		                        .hour        = c[2] & 0x1fu,
		                        .day         = c[4] & 0x1fu,
		                        .month       = c[5] & 0x0fu,
		                        .year        = (c[6] & 0x7fu) + 2000u };
	}

	Pmic_rtc(Env &env, Acpm &acpm) : _env(env), _acpm(acpm)
	{
		_env.parent().announce(_env.ep().manage(_root));
	}
};

#endif /* _SRC__DRIVER__PLATFORM__GS201__RTC_H_ */
