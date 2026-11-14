/*
 * \brief  Host test of Cpufreq::plan_requests (requests -> ordered rate changes)
 * \author Ivan Loskutov
 * \date   2026-10-03
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <cpufreq/request_policy.h>
#include <cstdio>
#include <initializer_list>

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

using namespace Cpufreq;

struct Soc { Cluster c[MAX_CLUSTERS]; };

static Soc soc(unsigned cl0, unsigned cl1, unsigned cl2)
{
	Soc s;
	s.c[0].id = 2; s.c[0].khz = cl0; s.c[0].min_khz = 300000; s.c[0].max_khz = 1803000;
	for (unsigned r : { 300000u, 574000u, 738000u, 930000u, 1098000u, 1197000u,
	                    1328000u, 1401000u, 1598000u, 1803000u })
		s.c[0].steps.add(r);

	s.c[1].id = 3; s.c[1].khz = cl1; s.c[1].min_khz = 400000; s.c[1].max_khz = 2348000;
	for (unsigned r : { 400000u, 799000u, 1197000u, 1491000u, 1999000u, 2348000u })
		s.c[1].steps.add(r);

	s.c[2].id = 4; s.c[2].khz = cl2; s.c[2].min_khz = 500000; s.c[2].max_khz = 2850000;
	for (unsigned r : { 500000u, 1106000u, 1826000u, 2048000u, 2850000u })
		s.c[2].steps.add(r);
	return s;
}

int main()
{
	/* constraint tables: rows of gs201-cpu.dtsi */
	CHECK(required_cl0_khz(1, 2348000) == 1328000);
	CHECK(required_cl0_khz(1, 1999000) == 1098000);
	CHECK(required_cl0_khz(1, 400000)  == 300000);
	CHECK(required_cl0_khz(2, 2850000) == 1401000);
	CHECK(required_cl0_khz(2, 2048000) == 1197000);
	CHECK(required_cl0_khz(2, 500000)  == 300000);
	/* a rate between two rows takes the row above it */
	CHECK(required_cl0_khz(2, 2000000) == 1197000);
	/* CL0 itself has no requirement */
	CHECK(required_cl0_khz(0, 1803000) == 0);

	/* nothing requested: nothing changes */
	{
		Soc const s = soc(1197000, 1197000, 1106000);
		Request const r[MAX_CLUSTERS] { };
		CHECK(plan_requests(s.c, r).count == 0);
	}

	/* request equal to the current rate: nothing changes */
	{
		Soc const s = soc(1197000, 1197000, 1106000);
		Request const r[MAX_CLUSTERS] { { true, 1197000 }, { }, { } };
		CHECK(plan_requests(s.c, r).count == 0);
	}

	/* big cluster up, little asked low: CL0 raised first, to the constraint */
	{
		Soc const s = soc(574000, 400000, 500000);
		Request const r[MAX_CLUSTERS] { { true, 574000 }, { }, { true, 2048000 } };
		Plan const p = plan_requests(s.c, r);
		CHECK(p.count == 2);
		CHECK(p.change[0].index == 0 && p.change[0].khz == 1197000);
		CHECK(p.change[1].index == 2 && p.change[1].khz == 2048000);
	}

	/* big cluster down, little down: CL0 lowered last */
	{
		Soc const s = soc(1197000, 400000, 2048000);
		Request const r[MAX_CLUSTERS] { { true, 574000 }, { }, { true, 500000 } };
		Plan const p = plan_requests(s.c, r);
		CHECK(p.count == 2);
		CHECK(p.change[0].index == 2 && p.change[0].khz == 500000);
		CHECK(p.change[1].index == 0 && p.change[1].khz == 574000);
	}

	/* a cluster that is not requested still constrains CL0 with its current rate */
	{
		Soc const s = soc(1197000, 400000, 2048000);
		Request const r[MAX_CLUSTERS] { { true, 300000 }, { }, { } };
		CHECK(plan_requests(s.c, r).count == 0);
	}

	/* request between two steps: rounded up */
	{
		Soc const s = soc(1197000, 1197000, 1106000);
		Request const r[MAX_CLUSTERS] { { }, { true, 1300000 }, { } };
		Plan const p = plan_requests(s.c, r);
		CHECK(p.count == 1 && p.change[0].index == 1 && p.change[0].khz == 1491000);
	}

	/* request beyond the hardware limits: clamped */
	{
		Soc const s = soc(1197000, 1197000, 1106000);
		Request const r[MAX_CLUSTERS] { { true, 9999999 }, { true, 1 }, { } };
		Plan const p = plan_requests(s.c, r);
		CHECK(p.count == 2);
		CHECK(p.change[0].index == 0 && p.change[0].khz == 1803000);
		CHECK(p.change[1].index == 1 && p.change[1].khz == 400000);
	}

	/* CL0 without a step table: the other clusters stay within what CL0 allows */
	{
		Soc s = soc(738000, 400000, 500000);
		s.c[0].steps = Steps();
		Request const r[MAX_CLUSTERS] { { true, 1803000 }, { true, 2348000 }, { true, 2850000 } };
		Plan const p = plan_requests(s.c, r);
		/* CL0 at 738 MHz allows CL1 up to 1197 MHz and CL2 up to 1106 MHz */
		CHECK(p.count == 2);
		CHECK(p.change[0].index == 1 && p.change[0].khz == 1197000);
		CHECK(p.change[1].index == 2 && p.change[1].khz == 1106000);
	}

	/* a cluster without a step table is never changed */
	{
		Soc s = soc(1197000, 1197000, 1106000);
		s.c[2].steps = Steps();
		Request const r[MAX_CLUSTERS] { { }, { }, { true, 2048000 } };
		CHECK(plan_requests(s.c, r).count == 0);
	}

	/* current rate that is not a step: a request to the nearest step is a change */
	{
		Soc const s = soc(1197000, 1197000, 1000000);
		Request const r[MAX_CLUSTERS] { { }, { }, { true, 1000000 } };
		Plan const p = plan_requests(s.c, r);
		CHECK(p.count == 1 && p.change[0].index == 2 && p.change[0].khz == 1106000);
	}

	/*
	 * Applying a plan: a change that does not take effect ends the plan, so
	 * that CL1/CL2 are never raised over a CL0 that did not follow.
	 */
	{
		struct Backend
		{
			unsigned fail_index;   /* this cluster keeps its rate */
			unsigned calls = 0;
			Cluster *clusters;

			unsigned set(unsigned index, unsigned khz)
			{
				calls++;
				return index == fail_index ? clusters[index].khz : khz;
			}
		};

		Request const r[MAX_CLUSTERS] { { true, 574000 }, { }, { true, 2048000 } };

		/* everything works: both changes applied, rates recorded */
		{
			Soc s = soc(574000, 400000, 500000);
			Backend b { 99, 0, s.c };
			CHECK(apply_plan(plan_requests(s.c, r), s.c, b));
			CHECK(b.calls == 2 && s.c[0].khz == 1197000 && s.c[2].khz == 2048000);
		}

		/* CL0 does not follow: CL2 is not touched */
		{
			Soc s = soc(574000, 400000, 500000);
			Backend b { 0, 0, s.c };
			CHECK(!apply_plan(plan_requests(s.c, r), s.c, b));
			CHECK(b.calls == 1 && s.c[0].khz == 574000 && s.c[2].khz == 500000);
		}

		/* the backend reports another rate than asked: that rate is recorded */
		{
			struct Odd { unsigned set(unsigned, unsigned) { return 930000; } } odd;
			Soc s = soc(574000, 400000, 500000);
			CHECK(!apply_plan(plan_requests(s.c, r), s.c, odd));
			CHECK(s.c[0].khz == 930000 && s.c[2].khz == 500000);
		}

		/* the backend could not send (0): the recorded rate stays */
		{
			struct Dead { unsigned set(unsigned, unsigned) { return 0; } } dead;
			Soc s = soc(574000, 400000, 500000);
			CHECK(!apply_plan(plan_requests(s.c, r), s.c, dead));
			CHECK(s.c[0].khz == 574000);
		}
	}

	/* ordering with CL1 and with both big clusters */
	{
		Soc const s = soc(574000, 400000, 500000);
		Request const r[MAX_CLUSTERS] { { }, { true, 2348000 }, { true, 2850000 } };
		Plan const p = plan_requests(s.c, r);
		CHECK(p.count == 3);
		CHECK(p.change[0].index == 0 && p.change[0].khz == 1401000);
		CHECK(p.change[1].index == 1 && p.change[2].index == 2);
	}

	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
