/*
 * Minimal host-test check helper shared by the test_*.cc programs
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _I2C_TEST__CHECK_H_
#define _I2C_TEST__CHECK_H_

#include <cstdio>

static int failures = 0, checks = 0;

#define CHECK(c) do { checks++; if (!(c)) { \
	std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define CHECK_EQ(a, b) do { checks++; auto _a = (a); auto _b = (b); if (!(_a == _b)) { \
	std::printf("FAIL %s:%d %s == %s  (0x%llx vs 0x%llx)\n", __FILE__, __LINE__, #a, #b, \
	            (unsigned long long)_a, (unsigned long long)_b); failures++; } } while (0)

static inline int check_result(char const *name)
{
	if (failures)
		std::printf("%s: FAILED (%d of %d checks)\n", name, failures, checks);
	else
		std::printf("%s: passed (%d checks)\n", name, checks);
	return failures ? 1 : 0;
}

#endif
