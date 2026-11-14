/*
 * \brief  Host test of Cpufreq::steps_from_table (firmware rate table -> steps)
 * \author Ivan Loskutov
 * \date   2026-10-03
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <cpufreq/types.h>
#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

using namespace Cpufreq;

int main()
{
	/* a table as the firmware gives it: strictly monotonic, either direction */
	{
		Steps s;
		unsigned const down[] { 1803000, 1197000, 574000, 300000 };
		CHECK(steps_from_table(down, 4, 300000, 1803000, s));
		CHECK(s.count == 4 && s.lowest() == 300000 && s.highest() == 1803000);

		unsigned const up[] { 300000, 574000, 1197000 };
		CHECK(steps_from_table(up, 3, 300000, 1803000, s) && s.count == 3);

		/* rates outside the limits are left out, the order still counts */
		unsigned const wide[] { 2000000, 1803000, 574000, 200000 };
		CHECK(steps_from_table(wide, 4, 300000, 1803000, s) && s.count == 2);

		/* garbage: not monotonic, repeated, empty, too long, nothing in range */
		unsigned const mixed[] { 1803000, 300000, 0xdeadbeef, 574000, 1 };
		CHECK(!steps_from_table(mixed, 5, 300000, 1803000, s));
		CHECK(s.count == 2);   /* an earlier table is not replaced by garbage */
		unsigned const twice[] { 1803000, 574000, 574000, 300000 };
		CHECK(!steps_from_table(twice, 4, 300000, 1803000, s));
		CHECK(!steps_from_table(up, 0, 300000, 1803000, s));
		unsigned many[MAX_STEPS + 1];
		for (unsigned i = 0; i <= MAX_STEPS; i++) many[i] = 300000 + i;
		CHECK(!steps_from_table(many, MAX_STEPS + 1, 300000, 1803000, s));
		unsigned const outside[] { 100, 200 };
		CHECK(!steps_from_table(outside, 2, 300000, 1803000, s));
	}


	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
