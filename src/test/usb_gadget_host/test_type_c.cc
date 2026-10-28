/* Host test: Usb_gadget::polarity (TCPCI CC_STATUS -> cable orientation) */
/* SPDX-License-Identifier: MIT */
#include <usb_gadget/type_c.h>
#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

using namespace Usb_gadget;

int main()
{
	/* polarity_cc1: Rp seen on CC1 (bits 1:0) */
	CHECK(polarity(0x01) == Polarity::CC1);
	CHECK(polarity(0x02) == Polarity::CC1);

	/* polarity_cc2: Rp seen on CC2 (bits 3:2) */
	CHECK(polarity(0x08) == Polarity::CC2);
	CHECK(polarity(0x04) == Polarity::CC2);

	/* polarity_none: nothing attached, or both lines active */
	CHECK(polarity(0x00) == Polarity::NONE);
	CHECK(polarity(0x05) == Polarity::NONE);

	/* the other bits (looking4connection, connect result) do not matter */
	CHECK(polarity(0x10 | 0x01) == Polarity::CC1);
	CHECK(polarity(0x20 | 0x08) == Polarity::CC2);

	std::printf("test_type_c: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
