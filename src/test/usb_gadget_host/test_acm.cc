/* Host test: Usb_gadget::handle (CDC ACM class requests) */
/* SPDX-License-Identifier: MIT */
#include <usb_gadget/acm.h>
#include <cstdio>
#include <cstring>

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

using namespace Usb_gadget;

/* the descriptors as raw bytes, as they passed on the phone */
static unsigned char const reference_fs[] = {
	9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 1,
	5, 0x24, 0x00, 0x10, 0x01,
	5, 0x24, 0x01, 0x00, 0x01,
	4, 0x24, 0x02, 0x02,
	5, 0x24, 0x06, 0x00, 0x01,
	7, 5, 0x81, 0x03, 10, 0, 32,
	9, 4, 1, 0, 2, 0x0a, 0x00, 0x00, 0,
	7, 5, 0x82, 0x02, 64, 0, 0,
	7, 5, 0x03, 0x02, 64, 0, 0 };

static unsigned char const reference_hs[] = {
	9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 1,
	5, 0x24, 0x00, 0x10, 0x01,
	5, 0x24, 0x01, 0x00, 0x01,
	4, 0x24, 0x02, 0x02,
	5, 0x24, 0x06, 0x00, 0x01,
	7, 5, 0x81, 0x03, 10, 0, 9,
	9, 4, 1, 0, 2, 0x0a, 0x00, 0x00, 0,
	7, 5, 0x82, 0x02, 0x00, 0x02, 0,
	7, 5, 0x03, 0x02, 0x00, 0x02, 0 };

static unsigned char const reference_ss[] = {
	9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 1,
	5, 0x24, 0x00, 0x10, 0x01,
	5, 0x24, 0x01, 0x00, 0x01,
	4, 0x24, 0x02, 0x02,
	5, 0x24, 0x06, 0x00, 0x01,
	7, 5, 0x81, 0x03, 10, 0, 9,
	6, 0x30, 0, 0, 10, 0,
	9, 4, 1, 0, 2, 0x0a, 0x00, 0x00, 0,
	7, 5, 0x82, 0x02, 0x00, 0x04, 0,
	6, 0x30, 0, 0, 0, 0,
	7, 5, 0x03, 0x02, 0x00, 0x04, 0,
	6, 0x30, 0, 0, 0, 0 };

static bool same(Blob b, unsigned char const *ref, size_t len)
{
	return b.len == len && std::memcmp(b.ptr, ref, len) == 0;
}

int main()
{
	/* the structs produce exactly the reference bytes */
	CHECK(same(blob(acm_fs), reference_fs, sizeof(reference_fs)));
	CHECK(same(blob(acm_hs), reference_hs, sizeof(reference_hs)));
	CHECK(same(blob(acm_ss), reference_ss, sizeof(reference_ss)));

	Acm acm { };
	unsigned char in[16] { };
	size_t in_len = 0;

	/* default line coding: 115200 8N1 */
	{
		unsigned char const expect[7] = { 0x00, 0xc2, 0x01, 0x00, 0, 0, 8 };
		CHECK(std::memcmp(acm.line_coding, expect, 7) == 0);
	}

	/* set_line_coding: first the request, then its data stage */
	{
		Setup const s { 0x21, 0x20, 0, 0, 7 };
		CHECK(handle(acm, s, nullptr, in, in_len) == Reply::DATA_OUT);

		unsigned char const data[7] = { 0x80, 0x25, 0x00, 0x00, 0, 0, 8 }; /* 9600 */
		CHECK(handle(acm, s, data, in, in_len) == Reply::STATUS);
		CHECK(std::memcmp(acm.line_coding, data, 7) == 0);
	}

	/* get_line_coding returns the stored value */
	{
		Setup const s { 0xa1, 0x21, 0, 0, 7 };
		in_len = 0;
		CHECK(handle(acm, s, nullptr, in, in_len) == Reply::DATA_IN);
		CHECK(in_len == 7);
		CHECK(std::memcmp(in, acm.line_coding, 7) == 0);
		CHECK(in[0] == 0x80 && in[1] == 0x25);
	}

	/* get_line_coding with a shorter wLength sends no more than asked */
	{
		Setup const s { 0xa1, 0x21, 0, 0, 4 };
		in_len = 0;
		CHECK(handle(acm, s, nullptr, in, in_len) == Reply::DATA_IN);
		CHECK(in_len == 4);
	}

	/* dtr: SET_CONTROL_LINE_STATE bit 0 */
	{
		CHECK(handle(acm, Setup { 0x21, 0x22, 1, 0, 0 }, nullptr, in, in_len) == Reply::STATUS);
		CHECK(acm.dtr == true);
		CHECK(handle(acm, Setup { 0x21, 0x22, 2, 0, 0 }, nullptr, in, in_len) == Reply::STATUS);
		CHECK(acm.dtr == false);
	}

	/* send_break is accepted */
	CHECK(handle(acm, Setup { 0x21, 0x23, 0xffff, 0, 0 }, nullptr, in, in_len) == Reply::STATUS);

	/* unknown_request_stalls: SET_COMM_FEATURE, and a known request with the wrong direction */
	CHECK(handle(acm, Setup { 0x21, 0x02, 0, 0, 2 }, nullptr, in, in_len) == Reply::STALL);
	CHECK(handle(acm, Setup { 0xa1, 0x20, 0, 0, 7 }, nullptr, in, in_len) == Reply::STALL);
	CHECK(handle(acm, Setup { 0x21, 0x21, 0, 0, 7 }, nullptr, in, in_len) == Reply::STALL);

	/* set_line_coding with a wrong length stalls */
	CHECK(handle(acm, Setup { 0x21, 0x20, 0, 0, 3 }, nullptr, in, in_len) == Reply::STALL);

	std::printf("test_acm: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
