/*
 * \brief  Host test of ctl_merge.h
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * The expectations follow Android's audio_route (system/media/audio_route),
 * as described in tool/mixer_paths_to_hid.py.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "ctl_merge.h"

static unsigned _failed, _checks;

#define CHECK(cond) do { \
	_checks++; \
	if (!(cond)) { _failed++; printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static int equal(long long const *a, long long const *b, unsigned n)
{
	return memcmp(a, b, n*sizeof(*a)) == 0;
}


static void test_numbers(void)
{
	long long v = 0;

	CHECK(ctl_merge_number("130", 3, 0, &v) == 0 && v == 130);
	CHECK(ctl_merge_number("-90", 3, 0, &v) == 0 && v == -90);
	CHECK(ctl_merge_number("+7",  2, 0, &v) == 0 && v == 7);
	CHECK(ctl_merge_number("0x1f", 4, 0, &v) == 0 && v == 31);
	CHECK(ctl_merge_number("010", 3, 0, &v) == 0 && v == 8);     /* octal, as strtol */
	CHECK(ctl_merge_number("0",   1, 0, &v) == 0 && v == 0);
	CHECK(ctl_merge_number("1065353216", 10, 0, &v) == 0 && v == 1065353216);
	CHECK(ctl_merge_number("ff",  2, 16, &v) == 0 && v == 255);  /* bytes */
	CHECK(ctl_merge_number("0xff", 4, 16, &v) == 0 && v == 255);

	/* only the given length counts */
	CHECK(ctl_merge_number("12 34", 2, 0, &v) == 0 && v == 12);

	CHECK(ctl_merge_number("",     0, 0, &v) == CTL_MERGE_BAD_VALUE);
	CHECK(ctl_merge_number("-",    1, 0, &v) == CTL_MERGE_BAD_VALUE);
	CHECK(ctl_merge_number("3.50A", 5, 0, &v) == CTL_MERGE_BAD_VALUE);
	CHECK(ctl_merge_number("ASP",  3, 0, &v) == CTL_MERGE_BAD_VALUE);
	CHECK(ctl_merge_number("09",   2, 0, &v) == CTL_MERGE_BAD_VALUE);
}


static void test_bool(void)
{
	struct ctl_merge_ctl const ctl = { .type = CTL_MERGE_BOOL, .count = 2 };
	long long vals[2] = { 7, 7 };

	/* one value for all elements */
	CHECK(ctl_merge_write(&ctl, -1, "1", vals) == 0);
	CHECK(equal(vals, (long long[]){ 1, 1 }, 2));

	CHECK(ctl_merge_write(&ctl, 1, "0", vals) == 0);
	CHECK(equal(vals, (long long[]){ 1, 0 }, 2));

	CHECK(ctl_merge_write(&ctl, 2, "1", vals) == CTL_MERGE_BAD_INDEX);
	CHECK(ctl_merge_write(&ctl, -1, "on", vals) == CTL_MERGE_BAD_VALUE);
	CHECK(equal(vals, (long long[]){ 1, 0 }, 2));   /* untouched by errors */
}


static void test_int(void)
{
	struct ctl_merge_ctl const ctl = { .type = CTL_MERGE_INT, .count = 4 };
	long long vals[4] = { 9, 9, 9, 9 };

	/* whole control: one number per element, the rest becomes zero */
	CHECK(ctl_merge_write(&ctl, -1, "0 3 2 -1", vals) == 0);
	CHECK(equal(vals, (long long[]){ 0, 3, 2, -1 }, 4));

	CHECK(ctl_merge_write(&ctl, -1, "817", vals) == 0);
	CHECK(equal(vals, (long long[]){ 817, 0, 0, 0 }, 4));

	CHECK(ctl_merge_write(&ctl, -1, "  5   6 ", vals) == 0);
	CHECK(equal(vals, (long long[]){ 5, 6, 0, 0 }, 4));

	/* more numbers than elements: the surplus is ignored */
	CHECK(ctl_merge_write(&ctl, -1, "1 2 3 4 5", vals) == 0);
	CHECK(equal(vals, (long long[]){ 1, 2, 3, 4 }, 4));

	/* with index: that element only, from the first number */
	CHECK(ctl_merge_write(&ctl, 2, "-1 99", vals) == 0);
	CHECK(equal(vals, (long long[]){ 1, 2, -1, 4 }, 4));

	CHECK(ctl_merge_write(&ctl, 4, "1", vals) == CTL_MERGE_BAD_INDEX);
	CHECK(ctl_merge_write(&ctl, -1, "", vals) == CTL_MERGE_BAD_VALUE);
	CHECK(ctl_merge_write(&ctl, -1, "1 x", vals) == CTL_MERGE_BAD_VALUE);
	CHECK(ctl_merge_write(&ctl, -1, "2.50A", vals) == CTL_MERGE_BAD_VALUE);
}


/*
 * A 'control' group as card.c applies it: the array starts with zeros,
 * adjacent index nodes of one control are merged in order
 */
static void test_index_group(void)
{
	struct ctl_merge_ctl const ctl = { .type = CTL_MERGE_INT, .count = 6 };
	long long vals[6] = { 0 };

	/* sidetone biquad: float bit patterns per element */
	CHECK(ctl_merge_write(&ctl, 0, "1065353216", vals) == 0);
	CHECK(ctl_merge_write(&ctl, 1, "1065353216", vals) == 0);
	CHECK(ctl_merge_write(&ctl, 5, "3", vals) == 0);
	CHECK(equal(vals, (long long[]){ 1065353216, 1065353216, 0, 0, 0, 3 }, 6));

	/* a whole-control node followed by element nodes */
	CHECK(ctl_merge_write(&ctl, -1, "1 1", vals) == 0);
	CHECK(ctl_merge_write(&ctl,  3, "8", vals) == 0);
	CHECK(equal(vals, (long long[]){ 1, 1, 0, 8, 0, 0 }, 6));
}


static void test_enum(void)
{
	static char const * const items[] = { "ASP", "DSP", 0, "2.50A" };

	struct ctl_merge_ctl const ctl = {
		.type = CTL_MERGE_ENUM, .count = 2, .items = items, .num_items = 4 };

	long long vals[2] = { 5, 5 };

	CHECK(ctl_merge_write(&ctl, -1, "DSP", vals) == 0);
	CHECK(equal(vals, (long long[]){ 1, 1 }, 2));

	/* an item text that looks like a number with a unit */
	CHECK(ctl_merge_write(&ctl, -1, "2.50A", vals) == 0);
	CHECK(equal(vals, (long long[]){ 3, 3 }, 2));

	CHECK(ctl_merge_write(&ctl, 1, "ASP", vals) == 0);
	CHECK(equal(vals, (long long[]){ 3, 0 }, 2));

	/* numeric fallback (audio_route: enum_mixer_numeric_fallback) */
	CHECK(ctl_merge_write(&ctl, -1, "1", vals) == 0);
	CHECK(equal(vals, (long long[]){ 1, 1 }, 2));

	CHECK(ctl_merge_write(&ctl, -1, "4",    vals) == CTL_MERGE_BAD_VALUE);
	CHECK(ctl_merge_write(&ctl, -1, "asp",  vals) == CTL_MERGE_BAD_VALUE);
	CHECK(ctl_merge_write(&ctl, -1, "DSP ", vals) == CTL_MERGE_BAD_VALUE);
	CHECK(equal(vals, (long long[]){ 1, 1 }, 2));

	/* a control whose items could not be listed */
	{
		struct ctl_merge_ctl const none = { .type = CTL_MERGE_ENUM, .count = 1 };
		CHECK(ctl_merge_write(&none, -1, "fast_switch5.txt", vals) == CTL_MERGE_BAD_VALUE);
		CHECK(ctl_merge_write(&none, -1, "0", vals) == CTL_MERGE_BAD_VALUE);
	}
}


static void test_bytes(void)
{
	struct ctl_merge_ctl const ctl = { .type = CTL_MERGE_BYTES, .count = 4 };
	long long vals[4] = { 1, 1, 1, 1 };

	CHECK(ctl_merge_write(&ctl, -1, "de ad 0xbe", vals) == 0);
	CHECK(equal(vals, (long long[]){ 0xde, 0xad, 0xbe, 0 }, 4));

	CHECK(ctl_merge_write(&ctl, 3, "10", vals) == 0);
	CHECK(vals[3] == 0x10);

	CHECK(ctl_merge_write(&ctl, -1, "zz", vals) == CTL_MERGE_BAD_VALUE);
}


int main(void)
{
	test_numbers();
	test_bool();
	test_int();
	test_index_group();
	test_enum();
	test_bytes();

	printf("ctl_merge: %u checks, %u failed\n", _checks, _failed);
	return _failed ? 1 : 0;
}
