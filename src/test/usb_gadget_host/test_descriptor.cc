/* Host test: Usb_gadget descriptor parsing, renumbering and composition */
/* SPDX-License-Identifier: MIT */
#include <usb_gadget/descriptor.h>
#include <usb_gadget/acm.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

using namespace Usb_gadget;

/* n-th descriptor of 'type' (and CS subtype if 'sub' >= 0) in a blob */
static unsigned char const *find(unsigned char const *p, size_t len,
                                 unsigned char type, unsigned n, int sub = -1)
{
	for (size_t o = 0; o + 2 <= len && p[o] >= 2; o += p[o])
		if (p[o + 1] == type && (sub < 0 || p[o + 2] == sub))
			if (n-- == 0)
				return p + o;
	return nullptr;
}

static unsigned count(unsigned char const *p, size_t len, unsigned char type)
{
	unsigned n = 0;
	for (size_t o = 0; o + 2 <= len && p[o] >= 2; o += p[o])
		if (p[o + 1] == type) n++;
	return n;
}

int main()
{
	Blob const hs = blob(acm_hs);
	Blob const ss = blob(acm_ss);
	Blob const fs = blob(acm_fs);

	/* parse_acm */
	for (Blob const &b : { fs, hs, ss }) {
		Function_info const i = parse(b);
		CHECK(i.valid);
		CHECK(i.interfaces == 2);
		CHECK(i.endpoints  == 3);
		CHECK(i.strings    == 1);
	}

	/* the ACM blobs: max packet size per speed, SS with companions */
	CHECK(find(fs.ptr, fs.len, 5, 1)[4] == 64);
	CHECK(find(hs.ptr, hs.len, 5, 1)[4] == 0x00 && find(hs.ptr, hs.len, 5, 1)[5] == 2);
	CHECK(find(ss.ptr, ss.len, 5, 1)[4] == 0x00 && find(ss.ptr, ss.len, 5, 1)[5] == 4);
	CHECK(count(ss.ptr, ss.len, 0x30) == 3);
	CHECK(count(hs.ptr, hs.len, 0x30) == 0);

	/* renumber_second_slot */
	{
		unsigned char const eps[3] = { 0x84, 0x85, 0x06 };
		Slot_map const map { 2, 4, eps, 3 };
		unsigned char out[256] { };
		size_t const n = compose_function(hs, map, out, sizeof(out));
		CHECK(n == hs.len + 8);   /* IAD prepended */

		unsigned char const *iad = find(out, n, 0x0b, 0);
		CHECK(iad == out);
		CHECK(iad && iad[0] == 8 && iad[2] == 2 && iad[3] == 2);
		CHECK(iad && iad[4] == 0x02 && iad[5] == 0x02 && iad[6] == 0x01);

		unsigned char const *if0 = find(out, n, 4, 0), *if1 = find(out, n, 4, 1);
		CHECK(if0 && if0[2] == 2 && if0[8] == 5);
		CHECK(if1 && if1[2] == 3 && if1[8] == 0);

		unsigned char const *un = find(out, n, 0x24, 0, 0x06);
		CHECK(un && un[3] == 2 && un[4] == 3);
		unsigned char const *cm = find(out, n, 0x24, 0, 0x01);
		CHECK(cm && cm[4] == 3);

		CHECK(find(out, n, 5, 0)[2] == 0x84);
		CHECK(find(out, n, 5, 1)[2] == 0x85);
		CHECK(find(out, n, 5, 2)[2] == 0x06);

		/* the input is untouched */
		CHECK(find(hs.ptr, hs.len, 4, 0)[2] == 0);
	}

	/* a function with an IAD of its own gets none added, its IAD is renumbered */
	{
		unsigned char out[256] { }, out2[256] { };
		unsigned char const eps[3] = { 0x81, 0x82, 0x01 };
		size_t const n = compose_function(hs, Slot_map { 0, 0, eps, 3 }, out, sizeof(out));
		size_t const n2 = compose_function(Blob { out, n }, Slot_map { 1, 0, eps, 3 }, out2, sizeof(out2));
		CHECK(n2 == n);
		CHECK(count(out2, n2, 0x0b) == 1);
		CHECK(find(out2, n2, 0x0b, 0)[2] == 1);
	}

	/* single-interface function: no IAD */
	{
		unsigned char const one[] = {
			9, 4, 0, 0, 2, 0xff, 0x42, 0x01, 0,
			7, 5, 0x81, 0x02, 0x00, 0x02, 0,
			7, 5, 0x01, 0x02, 0x00, 0x02, 0 };
		unsigned char const eps[2] = { 0x83, 0x02 };
		unsigned char out[64] { };
		size_t const n = compose_function(Blob { one, sizeof(one) }, Slot_map { 2, 0, eps, 2 }, out, sizeof(out));
		CHECK(n == sizeof(one));
		CHECK(count(out, n, 0x0b) == 0);
		CHECK(out[2] == 2 && out[11] == 0x83 && out[18] == 0x02);
	}

	/* config_header */
	{
		unsigned char f1[256], f2[256];
		unsigned char const e1[3] = { 0x81, 0x82, 0x01 }, e2[3] = { 0x83, 0x84, 0x02 };
		size_t const n1 = compose_function(hs, Slot_map { 0, 0, e1, 3 }, f1, sizeof(f1));
		size_t const n2 = compose_function(hs, Slot_map { 2, 1, e2, 3 }, f2, sizeof(f2));
		Blob const fns[2] = { { f1, n1 }, { f2, n2 } };

		unsigned char out[512] { };
		size_t const n = compose_config(fns, 2, Speed::HIGH, 100, out, sizeof(out));
		CHECK(n == 9 + n1 + n2);
		CHECK(out[0] == 9 && out[1] == 2);
		CHECK((out[2] | (out[3] << 8)) == (int)n);
		CHECK(out[4] == 4);      /* bNumInterfaces */
		CHECK(out[5] == 1);      /* bConfigurationValue */
		CHECK(out[7] == 0x80);   /* bus powered */
		CHECK(out[8] == 50);     /* 100 mA in 2 mA units */
		CHECK(std::memcmp(out + 9, f1, n1) == 0);
		CHECK(std::memcmp(out + 9 + n1, f2, n2) == 0);

		CHECK(compose_config(fns, 2, Speed::SUPER, 100, out, sizeof(out)) == n);
		CHECK(out[8] == 13);     /* 100 mA in 8 mA units, rounded up */
		CHECK(compose_config(fns, 2, Speed::FULL, 100, out, sizeof(out)) == n);
		CHECK(out[8] == 50);
	}

	/* reject_broken */
	{
		unsigned char const zero[] = { 0, 4, 0, 0 };
		CHECK(!parse(Blob { zero, sizeof(zero) }).valid);

		CHECK(!parse(Blob { hs.ptr, hs.len - 3 }).valid);  /* truncated */

		unsigned char const ep_first[] = { 7, 5, 0x81, 0x02, 0x00, 0x02, 0 };
		CHECK(!parse(Blob { ep_first, sizeof(ep_first) }).valid);

		unsigned char const gap[] = { 9, 4, 1, 0, 0, 0xff, 0, 0, 0 };  /* starts at 1 */
		CHECK(!parse(Blob { gap, sizeof(gap) }).valid);

		CHECK(!parse(Blob { nullptr, 0 }).valid);

		unsigned char out[256];
		unsigned char const eps[3] = { 0x81, 0x82, 0x01 };
		CHECK(compose_function(Blob { zero, sizeof(zero) }, Slot_map { 0, 0, eps, 3 }, out, sizeof(out)) == 0);
		CHECK(compose_function(hs, Slot_map { 0, 0, eps, 3 }, out, 20) == 0);      /* too small */
		CHECK(compose_function(hs, Slot_map { 0, 0, eps, 2 }, out, sizeof(out)) == 0); /* too few endpoints */

		size_t const n = compose_function(hs, Slot_map { 0, 0, eps, 3 }, out, sizeof(out));
		Blob const fns[1] = { { out, n } };
		unsigned char cfg[32];
		CHECK(compose_config(fns, 1, Speed::HIGH, 100, cfg, sizeof(cfg)) == 0);
	}

	/* alternate settings count once as an interface */
	{
		unsigned char const alt[] = {
			9, 4, 0, 0, 0, 0xff, 0, 0, 0,
			9, 4, 0, 1, 1, 0xff, 0, 0, 0,
			7, 5, 0x81, 0x01, 0x00, 0x04, 1 };
		Function_info const i = parse(Blob { alt, sizeof(alt) });
		CHECK(i.valid && i.interfaces == 1 && i.endpoints == 1);
	}

	std::printf("test_descriptor: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
