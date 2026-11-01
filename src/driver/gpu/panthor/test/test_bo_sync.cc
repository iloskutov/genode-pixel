/*
 * \brief  Host test: checks of the buffer-object requests of the session
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Covers ../bo_sync.h (flags of BO_CREATE, ranges and total of BO_SYNC:
 * everything a client controls) and the BO_SYNC and BO_QUERY_INFO messages.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <cstddef>
#include <cstdint>

#include <gpu/panthor_session.h>

#include "bo_sync.h"
#include "tunnel.h"
#include "check.h"

using namespace Gpu::Panthor;
using Tunnel::Check;
using std::uint32_t;
using std::uint64_t;

typedef unsigned long long ull;


static void test_create_flags()
{
	/* no WB_MMAP: uncached, whatever else is set (left to the kernel) */
	CHECK_EQ(bo_create_cached(0, 1), 0);
	CHECK_EQ(bo_create_cached(0, 0), 0);
	CHECK_EQ(bo_create_cached(BO_NO_MMAP, 1), 0);
	CHECK_EQ(bo_create_cached(0xfffffffcu, 1), 0);

	/* WB_MMAP: cached if switched on */
	CHECK_EQ(bo_create_cached(BO_WB_MMAP, 1), 1);
	CHECK_EQ(bo_create_cached(BO_WB_MMAP | 0x80u, 1), 1);

	/* refused: switched off, or an object that is never mapped */
	CHECK_EQ(bo_create_cached(BO_WB_MMAP, 0), -1);
	CHECK_EQ(bo_create_cached(BO_WB_MMAP | BO_NO_MMAP, 1), -1);
	CHECK_EQ(bo_create_cached(BO_WB_MMAP | BO_NO_MMAP, 0), -1);
	CHECK_EQ(bo_create_cached(0xffffffffu, 1), -1);

	CHECK_EQ((uint32_t)BO_SYNC_FLAG_NO_MMAP, (uint32_t)BO_NO_MMAP);
	CHECK_EQ((uint32_t)BO_SYNC_FLAG_WB_MMAP, (uint32_t)BO_WB_MMAP);
}


static void test_ranges()
{
	ull const size = 0x10000;
	ull const max  = ~0ull;

	/* inside */
	CHECK(bo_sync_range_valid(0, 0, size));
	CHECK(bo_sync_range_valid(0, 1, size));
	CHECK(bo_sync_range_valid(0, size, size));
	CHECK(bo_sync_range_valid(size - 1, 1, size));
	CHECK(bo_sync_range_valid(0x40, 0x1000, size));

	/* an empty range at the end is inside, behind the end it is not */
	CHECK( bo_sync_range_valid(size, 0, size));
	CHECK(!bo_sync_range_valid(size + 1, 0, size));

	/* one byte too far: refused, not clamped */
	CHECK(!bo_sync_range_valid(0, size + 1, size));
	CHECK(!bo_sync_range_valid(1, size, size));
	CHECK(!bo_sync_range_valid(size - 1, 2, size));
	CHECK(!bo_sync_range_valid(size, 1, size));

	/* offset + size wraps */
	CHECK(!bo_sync_range_valid(max, 1, size));
	CHECK(!bo_sync_range_valid(1, max, size));
	CHECK(!bo_sync_range_valid(max, max, size));
	CHECK(!bo_sync_range_valid(max - 0xfff, 0x1000, size));
	CHECK(!bo_sync_range_valid(0x8000000000000000ull, 0x8000000000000000ull, size));
	CHECK(!bo_sync_range_valid(0x40, max - 0x3f, size));     /* sum = 0 */
	CHECK(!bo_sync_range_valid(0x40, max - 0x3f + 1, size)); /* sum = 1 */

	/* object of size 0 and of the maximal size */
	CHECK( bo_sync_range_valid(0, 0, 0));
	CHECK(!bo_sync_range_valid(0, 1, 0));
	CHECK(!bo_sync_range_valid(1, 0, 0));
	CHECK( bo_sync_range_valid(0, max, max));
	CHECK( bo_sync_range_valid(max, 0, max));
	CHECK(!bo_sync_range_valid(1, max, max));

	/* exhaustive around the end of a small object */
	for (ull obj = 0; obj < 20; obj++)
		for (ull off = 0; off < 24; off++)
			for (ull len = 0; len < 24; len++)
				CHECK_EQ(bo_sync_range_valid(off, len, obj) != 0,
				         off + len <= obj);
}


static void test_total()
{
	ull const limit = BO_SYNC_MAX_BYTES;
	ull total = 0;

	CHECK_EQ((uint64_t)limit, (uint64_t)MAX_BO_SYNC_BYTES);

	CHECK(bo_sync_account(&total, 0));
	CHECK(bo_sync_account(&total, limit - 1));
	CHECK(bo_sync_account(&total, 1));
	CHECK_EQ(total, limit);

	/* full: only empty ranges are accepted, the total stays */
	CHECK( bo_sync_account(&total, 0));
	CHECK(!bo_sync_account(&total, 1));
	CHECK(!bo_sync_account(&total, ~0ull));
	CHECK_EQ(total, limit);

	/* a single range above the limit, and sizes that would wrap the sum */
	total = 0;
	CHECK(!bo_sync_account(&total, limit + 1));
	CHECK(!bo_sync_account(&total, ~0ull));
	CHECK_EQ(total, 0ull);

	total = 1;
	CHECK(!bo_sync_account(&total, ~0ull));
	CHECK(!bo_sync_account(&total, limit));
	CHECK_EQ(total, 1ull);

	/* a total that is already invalid stays refused */
	total = limit + 5;
	CHECK(!bo_sync_account(&total, 0));
	total = ~0ull;
	CHECK(!bo_sync_account(&total, 1));

	/* many ranges: the sum counts, not the single size */
	total = 0;
	unsigned accepted = 0;
	for (unsigned i = 0; i < 4096; i++)
		if (bo_sync_account(&total, 1024*1024))
			accepted++;
	CHECK_EQ(accepted, 64u);
	CHECK_EQ(total, limit);
}


static void test_messages()
{
	alignas(16) static char bytes[8192];

	auto check = [&] { return Tunnel::check(bytes, ((Header *)bytes)->size); };

	/* BO_SYNC: the array must lie inside the message */
	{
		Message     msg(bytes, sizeof(bytes), Op::BO_SYNC);
		Bo_sync    &sync = msg.payload<Bo_sync>();
		Bo_sync_op *ops  = msg.append<Bo_sync_op>(4);

		CHECK(ops != nullptr);
		sync.ops = msg.obj_array(ops, 4);
		CHECK(msg.finish());
		CHECK(check() == Check::OK);

		/* the content of the ops is not the tunnel's business */
		ops[0] = { .handle = ~0u, .type = ~0u, .offset = ~0ull, .size = ~0ull };
		CHECK(check() == Check::OK);

		sync.ops.count = 5;
		CHECK(check() == Check::BAD_REFERENCE);
		sync.ops.count = ~0u;
		CHECK(check() == Check::BAD_REFERENCE);
		sync.ops.count = 4;

		sync.ops.stride = sizeof(Bo_sync_op) - 8;
		CHECK(check() == Check::BAD_REFERENCE);
		sync.ops.stride = sizeof(Bo_sync_op) + 4;
		CHECK(check() == Check::BAD_REFERENCE);
		sync.ops.stride = sizeof(Bo_sync_op);

		sync.ops.array = 0;
		CHECK(check() == Check::BAD_REFERENCE);
		sync.ops.array = sizeof(Header);
		CHECK(check() == Check::BAD_REFERENCE);
		sync.ops.array = ~0ull - 7;
		CHECK(check() == Check::BAD_REFERENCE);

		/* no ops: valid, the reference is not looked at */
		sync.ops.count = 0;
		CHECK(check() == Check::OK);
	}

	/* BO_QUERY_INFO: no references */
	{
		Message        msg(bytes, sizeof(bytes), Op::BO_QUERY_INFO);
		Bo_query_info &info = msg.payload<Bo_query_info>();

		info.handle = 7;
		CHECK(msg.finish());
		CHECK_EQ(msg.size(), sizeof(Header) + sizeof(Bo_query_info));
		CHECK(check() == Check::OK);

		((Header *)bytes)->size = sizeof(Header) + sizeof(Bo_query_info) - 4;
		CHECK(check() == Check::BAD_HEADER);
	}

	/* the request types of protocol 1.1 */
	CHECK_EQ((uint32_t)Op::BO_SYNC,       0x301u);
	CHECK_EQ((uint32_t)Op::BO_QUERY_INFO, 0x303u);

	/* a client of protocol 1.0 is still accepted */
	{
		Message msg(bytes, sizeof(bytes), Op::NOP);
		CHECK(msg.finish());
		((Header *)bytes)->version = (VERSION_MAJOR << 16) | 0;
		CHECK(check() == Check::OK);
		CHECK((uint32_t)VERSION_MINOR >= 1u);
	}
}


int main()
{
	test_create_flags();
	test_ranges();
	test_total();
	test_messages();

	return check_result("test_bo_sync");
}
