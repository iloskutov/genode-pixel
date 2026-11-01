/*
 * \brief  Host test: validation and relocation of Gpu-session messages
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Covers ../tunnel.h (what the driver does with a message before and after
 * 'drm_ioctl') and the 'Message' utility of the protocol header (what a
 * client does to compose one).
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <cstring>
#include <vector>

#include <tunnel.h>

#include "check.h"

using namespace Gpu::Panthor;
using Tunnel::Check;

using std::uint8_t;
using std::uint32_t;
using std::uint64_t;

enum { BUFFER_SIZE = 8192 };

/* aligned storage for one message */
struct Buffer
{
	alignas(16) char bytes[BUFFER_SIZE] { };

	Header &header() { return *(Header *)bytes; }

	template <typename T> T &payload() { return *(T *)(bytes + sizeof(Header)); }

	Check check() { return Tunnel::check(bytes, header().size); }
};


static Op const all_ops[] = {
	Op::NOP, Op::DEV_QUERY, Op::VM_CREATE, Op::VM_DESTROY, Op::VM_BIND,
	Op::VM_GET_STATE, Op::BO_CREATE, Op::GROUP_CREATE, Op::GROUP_DESTROY,
	Op::GROUP_SUBMIT, Op::GROUP_GET_STATE, Op::TILER_HEAP_CREATE,
	Op::TILER_HEAP_DESTROY, Op::BO_SET_LABEL, Op::GEM_CLOSE,
	Op::SYNCOBJ_CREATE, Op::SYNCOBJ_DESTROY, Op::SYNCOBJ_HANDLE_TO_FD,
	Op::SYNCOBJ_FD_TO_HANDLE, Op::SYNCOBJ_WAIT, Op::SYNCOBJ_RESET,
	Op::SYNCOBJ_SIGNAL, Op::SYNCOBJ_TIMELINE_WAIT, Op::SYNCOBJ_QUERY,
	Op::SYNCOBJ_TRANSFER, Op::SYNCOBJ_TIMELINE_SIGNAL, Op::FD_CLOSE,
	Op::BO_SYNC, Op::TEST_SLEEP, Op::BO_QUERY_INFO,
};


static void test_sizes()
{
	CHECK_EQ(sizeof(Header),                32u);
	CHECK_EQ(sizeof(Obj_array),             16u);
	CHECK_EQ(sizeof(Sync_op),               16u);
	CHECK_EQ(sizeof(Gpu_info),              112u);
	CHECK_EQ(sizeof(Csif_info),             24u);
	CHECK_EQ(sizeof(Timestamp_info),        24u);
	CHECK_EQ(sizeof(Group_priorities_info), 4u);
	CHECK_EQ(sizeof(Dev_query),             16u);
	CHECK_EQ(sizeof(Vm_create),             16u);
	CHECK_EQ(sizeof(Vm_bind_op),            48u);
	CHECK_EQ(sizeof(Vm_bind),               24u);
	CHECK_EQ(sizeof(Bo_create),             24u);
	CHECK_EQ(sizeof(Queue_create),          8u);
	CHECK_EQ(sizeof(Group_create),          56u);
	CHECK_EQ(sizeof(Queue_submit),          40u);
	CHECK_EQ(sizeof(Group_submit),          24u);
	CHECK_EQ(sizeof(Group_get_state),       16u);
	CHECK_EQ(sizeof(Tiler_heap_create),     40u);
	CHECK_EQ(sizeof(Bo_set_label),          16u);
	CHECK_EQ(sizeof(Syncobj_handle),        24u);
	CHECK_EQ(sizeof(Syncobj_transfer),      32u);
	CHECK_EQ(sizeof(Syncobj_wait),          40u);
	CHECK_EQ(sizeof(Syncobj_timeline_wait), 48u);
	CHECK_EQ(sizeof(Syncobj_array),         16u);
	CHECK_EQ(sizeof(Syncobj_timeline_array),24u);
	CHECK_EQ(sizeof(Bo_sync_op),            24u);
	CHECK_EQ(sizeof(Bo_query_info),         16u);
	CHECK_EQ(sizeof(Info) % 8,              0u);
	CHECK(sizeof(Info) <= 4096);

	/* every request type has a payload size, unknown ones do not */
	for (Op const op : all_ops)
		CHECK(Tunnel::payload_size(op) != Tunnel::UNKNOWN_PAYLOAD);

	CHECK_EQ(Tunnel::payload_size((Op)0x106), (uint32_t)Tunnel::UNKNOWN_PAYLOAD);
	CHECK_EQ(Tunnel::payload_size((Op)0x10e), (uint32_t)Tunnel::UNKNOWN_PAYLOAD);
	CHECK_EQ(Tunnel::payload_size((Op)1),     (uint32_t)Tunnel::UNKNOWN_PAYLOAD);
}


static void test_header()
{
	/* a message of each type with a zeroed payload is valid */
	for (Op const op : all_ops) {
		Buffer  b;
		Message msg(b.bytes, BUFFER_SIZE, op);

		(void)msg.append<char>(Tunnel::payload_size(op), 8);
		CHECK(msg.finish());
		CHECK(b.check() == Check::OK);

		/* ... but not if the payload is cut */
		if (Tunnel::payload_size(op)) {
			b.header().size -= 1;
			CHECK(b.check() == Check::BAD_HEADER);
		}
	}

	Buffer  b;
	Message msg(b.bytes, BUFFER_SIZE, Op::NOP);
	CHECK(msg.finish());
	CHECK_EQ(b.header().size, 32u);
	CHECK_EQ(b.header().magic, (uint32_t)MAGIC);
	CHECK_EQ(b.header().state, (uint32_t)Header::NEW);
	CHECK(b.check() == Check::OK);

	b.header().magic ^= 1;
	CHECK(b.check() == Check::BAD_HEADER);
	b.header().magic ^= 1;

	/* another minor version is accepted, another major version is not */
	b.header().version = (VERSION_MAJOR << 16) | 0xffff;
	CHECK(b.check() == Check::OK);
	b.header().version = (VERSION_MAJOR + 1) << 16;
	CHECK(b.check() == Check::BAD_VERSION);
	b.header().version = 0;
	CHECK(b.check() == Check::BAD_VERSION);
	b.header().version = VERSION;

	b.header().size = sizeof(Header) - 1;
	CHECK(Tunnel::check(b.bytes, BUFFER_SIZE) == Check::BAD_HEADER);

	/* more than the caller has copied */
	b.header().size = 64;
	CHECK(Tunnel::check(b.bytes, 63) == Check::BAD_HEADER);
	CHECK(Tunnel::check(b.bytes, 31) == Check::BAD_HEADER);
	CHECK(Tunnel::check(b.bytes, 64) == Check::OK);

	/* more than a message may have */
	{
		std::vector<char> big(MAX_MESSAGE_SIZE + 64);
		Header &h = *(Header *)big.data();
		h = b.header();
		h.size = MAX_MESSAGE_SIZE;
		CHECK(Tunnel::check(big.data(), big.size()) == Check::OK);
		h.size = MAX_MESSAGE_SIZE + 8;
		CHECK(Tunnel::check(big.data(), big.size()) == Check::BAD_HEADER);
	}

	b.header().size = 32;
	b.header().op   = 0x7777;
	CHECK(b.check() == Check::UNKNOWN_OP);
}


static void test_dev_query()
{
	Buffer     b;
	Message    msg(b.bytes, BUFFER_SIZE, Op::DEV_QUERY);
	Dev_query &q    = msg.payload<Dev_query>();
	Gpu_info  *info = msg.append<Gpu_info>(1);

	q.size    = sizeof(Gpu_info);
	q.pointer = msg.offset(info);
	CHECK(msg.finish());

	CHECK_EQ(q.pointer, 48u);
	CHECK_EQ(b.header().size, 48u + 112u);
	CHECK(b.check() == Check::OK);

	/* size query: no reference */
	q.pointer = 0;
	CHECK(b.check() == Check::OK);

	/* into the header, into the payload */
	q.pointer = 8;
	CHECK(b.check() == Check::BAD_REFERENCE);
	q.pointer = 40;
	CHECK(b.check() == Check::BAD_REFERENCE);

	/* unaligned */
	q.pointer = 52;
	q.size    = 8;
	CHECK(b.check() == Check::BAD_REFERENCE);

	/* one byte too long, far too long, wrapping */
	q.pointer = 48;
	q.size    = 113;
	CHECK(b.check() == Check::BAD_REFERENCE);
	q.size    = 0xffffffffu;
	CHECK(b.check() == Check::BAD_REFERENCE);
	q.size    = 112;
	q.pointer = ~0ull - 7;
	CHECK(b.check() == Check::BAD_REFERENCE);
	q.pointer = 1ull << 32 | 48;
	CHECK(b.check() == Check::BAD_REFERENCE);

	/* an empty result buffer right at the end is in range */
	q.pointer = b.header().size;
	q.size    = 0;
	CHECK(b.check() == Check::OK);
	q.size    = 1;
	CHECK(b.check() == Check::BAD_REFERENCE);

	/* relocation and back */
	q.pointer = 48;
	q.size    = 112;
	Buffer const before = b;

	uint64_t const base = 0x7f0012340000ull;
	Tunnel::relocate(b.bytes, base);
	CHECK_EQ(q.pointer, base + 48);
	CHECK_EQ(q.size, 112u);

	Tunnel::unrelocate(b.bytes, base);
	CHECK(!memcmp(b.bytes, before.bytes, BUFFER_SIZE));

	/* a size query is left alone */
	q.pointer = 0;
	Tunnel::relocate(b.bytes, base);
	CHECK_EQ(q.pointer, 0u);
	Tunnel::unrelocate(b.bytes, base);
	CHECK_EQ(q.pointer, 0u);
}


/* element of a newer uAPI revision: longer than the structure known here */
struct Wide_bind_op
{
	Vm_bind_op op;
	uint64_t   extension;
};


static void test_vm_bind()
{
	Buffer      b;
	Message     msg(b.bytes, BUFFER_SIZE, Op::VM_BIND);
	Vm_bind    &bind  = msg.payload<Vm_bind>();
	Vm_bind_op *ops   = msg.append<Vm_bind_op>(3);
	Sync_op    *syncs = msg.append<Sync_op>(4);

	bind.vm_id   = 7;
	bind.flags   = VM_BIND_ASYNC;
	bind.ops     = msg.obj_array(ops, 3);
	ops[0].syncs = msg.obj_array(&syncs[0], 1);
	ops[1].syncs = msg.obj_array(&syncs[1], 3);
	/* ops[2] has no syncs */
	ops[1].va    = 0x1234000;
	CHECK(msg.finish());

	CHECK_EQ(bind.ops.stride, 48u);
	CHECK_EQ(bind.ops.array, 32u + 24u);
	CHECK_EQ(ops[0].syncs.array, 56u + 3*48u);
	CHECK_EQ(ops[1].syncs.array, 56u + 3*48u + 16u);
	CHECK_EQ(ops[2].syncs.array, 0u);
	CHECK(b.check() == Check::OK);

	Buffer const   before = b;
	uint64_t const base   = (uint64_t)b.bytes;

	Tunnel::relocate(b.bytes, base);

	/* the references are addresses of the elements now */
	CHECK_EQ(bind.ops.array,     (uint64_t)&ops[0]);
	CHECK_EQ(ops[0].syncs.array, (uint64_t)&syncs[0]);
	CHECK_EQ(ops[1].syncs.array, (uint64_t)&syncs[1]);
	CHECK_EQ(ops[2].syncs.array, 0u);
	CHECK_EQ(ops[1].va, 0x1234000u);
	CHECK_EQ(bind.vm_id, 7u);

	Tunnel::unrelocate(b.bytes, base);
	CHECK(!memcmp(b.bytes, before.bytes, BUFFER_SIZE));

	/* nested array reaches behind the message */
	ops[1].syncs.count = 4;
	CHECK(b.check() == Check::BAD_REFERENCE);
	ops[1].syncs.count = 3;

	/* nested array of the last element is checked, too */
	ops[2].syncs = { .stride = 16, .count = 1, .array = b.header().size };
	CHECK(b.check() == Check::BAD_REFERENCE);
	ops[2].syncs = { .stride = 16, .count = 1, .array = b.header().size - 16 };
	CHECK(b.check() == Check::OK);
	ops[2].syncs = { };

	/* stride below the structure's size, not a multiple of 8, huge */
	ops[0].syncs.stride = 8;
	CHECK(b.check() == Check::BAD_REFERENCE);
	ops[0].syncs.stride = 20;
	CHECK(b.check() == Check::BAD_REFERENCE);
	ops[0].syncs.stride = 16;

	bind.ops.stride = 40;
	CHECK(b.check() == Check::BAD_REFERENCE);
	bind.ops.stride = Tunnel::MAX_STRIDE + 8;
	CHECK(b.check() == Check::BAD_REFERENCE);
	bind.ops.stride = 48;

	/* count that makes stride * count wrap in 32 bit */
	bind.ops.count = 0x05555556;   /* * 48 = 0x1'0000'0020 */
	CHECK(b.check() == Check::BAD_REFERENCE);
	bind.ops.count = 0xffffffffu;
	CHECK(b.check() == Check::BAD_REFERENCE);
	bind.ops.count = 3;

	/* the element array must not overlap the payload */
	bind.ops.array = 32;
	CHECK(b.check() == Check::BAD_REFERENCE);
	bind.ops.array = 56;
	CHECK(b.check() == Check::OK);

	/* an empty array needs no reference, its array member is not touched */
	bind.ops = { .stride = 0, .count = 0, .array = 0xdeadbeef };
	CHECK(b.check() == Check::OK);
	Tunnel::relocate(b.bytes, base);
	CHECK_EQ(bind.ops.array, 0xdeadbeefu);
	Tunnel::unrelocate(b.bytes, base);
	CHECK_EQ(bind.ops.array, 0xdeadbeefu);

	/* elements with a larger stride: the nested arrays are found by stride */
	{
		Buffer        w;
		Message       wide(w.bytes, BUFFER_SIZE, Op::VM_BIND);
		Vm_bind      &wbind  = wide.payload<Vm_bind>();
		Wide_bind_op *wops   = wide.append<Wide_bind_op>(2);
		Sync_op      *wsyncs = wide.append<Sync_op>(2);

		wbind.ops        = wide.obj_array(wops, 2);
		wops[0].op.syncs = wide.obj_array(&wsyncs[0], 1);
		wops[1].op.syncs = wide.obj_array(&wsyncs[1], 1);
		CHECK(wide.finish());

		CHECK_EQ(wbind.ops.stride, 56u);
		CHECK(w.check() == Check::OK);

		Tunnel::relocate(w.bytes, (uint64_t)w.bytes);
		CHECK_EQ(wops[1].op.syncs.array, (uint64_t)&wsyncs[1]);
		CHECK_EQ(wops[0].extension, 0u);
		Tunnel::unrelocate(w.bytes, (uint64_t)w.bytes);
		CHECK_EQ(wops[1].op.syncs.array, (uint64_t)((char *)&wsyncs[1] - w.bytes));

		wops[1].op.syncs.array += 16;
		CHECK(w.check() == Check::BAD_REFERENCE);
	}
}


static void test_group()
{
	/* GROUP_CREATE */
	{
		Buffer        b;
		Message       msg(b.bytes, BUFFER_SIZE, Op::GROUP_CREATE);
		Group_create &create = msg.payload<Group_create>();
		Queue_create *queues = msg.append<Queue_create>(3);

		create.queues = msg.obj_array(queues, 3);
		CHECK(msg.finish());
		CHECK(b.check() == Check::OK);

		Tunnel::relocate(b.bytes, 0x1000);
		CHECK_EQ(create.queues.array, 0x1000u + 32u + 56u);
		Tunnel::unrelocate(b.bytes, 0x1000);
		CHECK_EQ(create.queues.array, 32u + 56u);

		create.queues.count = 4;
		CHECK(b.check() == Check::BAD_REFERENCE);

		/* stride * count = 0x1'0000'0008: fits only if computed in 32 bit */
		create.queues.count = 0x20000001;
		CHECK(b.check() == Check::BAD_REFERENCE);

		create.queues.count  = 3;
		create.queues.stride = 4;
		CHECK(b.check() == Check::BAD_REFERENCE);
	}

	/* GROUP_SUBMIT: the syncs of each queue submit */
	{
		Buffer        b;
		Message       msg(b.bytes, BUFFER_SIZE, Op::GROUP_SUBMIT,
		                  Header::FLAG_SAMPLE_FLUSH_ID);
		Group_submit &submit = msg.payload<Group_submit>();
		Queue_submit *queues = msg.append<Queue_submit>(2);
		Sync_op      *syncs  = msg.append<Sync_op>(5);

		submit.queue_submits = msg.obj_array(queues, 2);
		queues[0].syncs      = msg.obj_array(&syncs[0], 2);
		queues[1].syncs      = msg.obj_array(&syncs[2], 3);
		queues[1].stream_addr = 0xabcd0000;
		CHECK(msg.finish());

		CHECK_EQ(b.header().flags, (uint32_t)Header::FLAG_SAMPLE_FLUSH_ID);
		CHECK(b.check() == Check::OK);

		Buffer const before = b;

		Tunnel::relocate(b.bytes, (uint64_t)b.bytes);
		CHECK_EQ(submit.queue_submits.array, (uint64_t)&queues[0]);
		CHECK_EQ(queues[0].syncs.array, (uint64_t)&syncs[0]);
		CHECK_EQ(queues[1].syncs.array, (uint64_t)&syncs[2]);
		CHECK_EQ(queues[1].stream_addr, 0xabcd0000u);   /* a GPU address */
		Tunnel::unrelocate(b.bytes, (uint64_t)b.bytes);
		CHECK(!memcmp(b.bytes, before.bytes, BUFFER_SIZE));

		queues[1].syncs.count = 4;
		CHECK(b.check() == Check::BAD_REFERENCE);
		queues[1].syncs.count = 3;

		/* a queue submit must hold all members up to 'syncs' */
		submit.queue_submits.stride = 32;
		CHECK(b.check() == Check::BAD_REFERENCE);
	}

	/* BO_SYNC */
	{
		Buffer      b;
		Message     msg(b.bytes, BUFFER_SIZE, Op::BO_SYNC);
		Bo_sync    &sync = msg.payload<Bo_sync>();
		Bo_sync_op *ops  = msg.append<Bo_sync_op>(2);

		sync.ops = msg.obj_array(ops, 2);
		CHECK(msg.finish());
		CHECK(b.check() == Check::OK);

		sync.ops.count = 3;
		CHECK(b.check() == Check::BAD_REFERENCE);
	}
}


static void test_label()
{
	Buffer        b;
	Message       msg(b.bytes, BUFFER_SIZE, Op::BO_SET_LABEL);
	Bo_set_label &label = msg.payload<Bo_set_label>();
	char         *text  = msg.append_string("vertex buffer");

	label.label = msg.offset(text);
	CHECK(msg.finish());

	CHECK_EQ(label.label, 48u);
	CHECK_EQ(b.header().size, 48u + 14u);
	CHECK(!strcmp(b.bytes + 48, "vertex buffer"));
	CHECK(b.check() == Check::OK);

	Tunnel::relocate(b.bytes, (uint64_t)b.bytes);
	CHECK(!strcmp((char const *)label.label, "vertex buffer"));
	Tunnel::unrelocate(b.bytes, (uint64_t)b.bytes);
	CHECK_EQ(label.label, 48u);

	/* a string may start anywhere behind the payload */
	label.label = 55;
	CHECK(b.check() == Check::OK);

	/* no terminating null inside of the message */
	b.header().size -= 1;
	CHECK(b.check() == Check::BAD_REFERENCE);
	b.header().size += 1;

	/* the empty string at the last byte is fine, the end is not */
	label.label = b.header().size - 1;
	CHECK(b.check() == Check::OK);
	label.label = b.header().size;
	CHECK(b.check() == Check::BAD_REFERENCE);

	label.label = 40;
	CHECK(b.check() == Check::BAD_REFERENCE);

	/* no label */
	label.label = 0;
	CHECK(b.check() == Check::OK);

	/* a string longer than the limit counts as unterminated */
	{
		std::vector<char> big(16384, 'x');
		Message long_msg(big.data(), big.size(), Op::BO_SET_LABEL);
		Bo_set_label &l = long_msg.payload<Bo_set_label>();
		char *s = long_msg.append<char>(Tunnel::MAX_STRING + 100, 1);
		memset(s, 'a', Tunnel::MAX_STRING + 99);
		l.label = long_msg.offset(s);
		CHECK(long_msg.finish());
		CHECK(Tunnel::check(big.data(), big.size()) == Check::BAD_REFERENCE);
		s[Tunnel::MAX_STRING - 1] = 0;
		CHECK(Tunnel::check(big.data(), big.size()) == Check::OK);
	}
}


static void test_syncobj()
{
	/* SYNCOBJ_WAIT */
	{
		Buffer        b;
		Message       msg(b.bytes, BUFFER_SIZE, Op::SYNCOBJ_WAIT);
		Syncobj_wait &wait    = msg.payload<Syncobj_wait>();
		uint32_t     *handles = msg.append<uint32_t>(3, 4);

		wait.handles       = msg.offset(handles);
		wait.count_handles = 3;
		wait.timeout_nsec  = 1000000;
		CHECK(msg.finish());

		CHECK_EQ(wait.handles, 72u);
		CHECK_EQ(b.header().size, 84u);
		CHECK(b.check() == Check::OK);

		Tunnel::relocate(b.bytes, (uint64_t)b.bytes);
		CHECK_EQ(wait.handles, (uint64_t)handles);
		CHECK_EQ(wait.timeout_nsec, 1000000);
		Tunnel::unrelocate(b.bytes, (uint64_t)b.bytes);
		CHECK_EQ(wait.handles, 72u);

		wait.count_handles = 4;
		CHECK(b.check() == Check::BAD_REFERENCE);
		wait.count_handles = 0x40000001;   /* * 4 wraps in 32 bit */
		CHECK(b.check() == Check::BAD_REFERENCE);
		wait.count_handles = 3;

		wait.handles = 74;                 /* handles are 32-bit aligned */
		CHECK(b.check() == Check::BAD_REFERENCE);

		/* handles are mandatory once there is a count */
		wait.handles = 0;
		CHECK(b.check() == Check::BAD_REFERENCE);
		wait.count_handles = 0;
		CHECK(b.check() == Check::OK);
	}

	/* SYNCOBJ_TIMELINE_WAIT: the points are optional */
	{
		Buffer                 b;
		Message                msg(b.bytes, BUFFER_SIZE, Op::SYNCOBJ_TIMELINE_WAIT);
		Syncobj_timeline_wait &wait    = msg.payload<Syncobj_timeline_wait>();
		uint64_t              *points  = msg.append<uint64_t>(2);
		uint32_t              *handles = msg.append<uint32_t>(2, 4);

		wait.handles       = msg.offset(handles);
		wait.points        = msg.offset(points);
		wait.count_handles = 2;
		CHECK(msg.finish());
		CHECK(b.check() == Check::OK);

		Tunnel::relocate(b.bytes, 0x10000);
		CHECK_EQ(wait.points,  0x10000u + 80u);
		CHECK_EQ(wait.handles, 0x10000u + 96u);
		Tunnel::unrelocate(b.bytes, 0x10000);
		CHECK_EQ(wait.points, 80u);

		wait.points = 84;                  /* points are 64-bit aligned */
		CHECK(b.check() == Check::BAD_REFERENCE);

		wait.points = 0;
		CHECK(b.check() == Check::OK);
		Tunnel::relocate(b.bytes, 0x10000);
		CHECK_EQ(wait.points, 0u);
		Tunnel::unrelocate(b.bytes, 0x10000);
		CHECK_EQ(wait.points, 0u);
		CHECK_EQ(wait.handles, 96u);
	}

	/* SYNCOBJ_QUERY writes the points: mandatory. TIMELINE_SIGNAL: optional. */
	for (Op const op : { Op::SYNCOBJ_QUERY, Op::SYNCOBJ_TIMELINE_SIGNAL }) {
		Buffer                  b;
		Message                 msg(b.bytes, BUFFER_SIZE, op);
		Syncobj_timeline_array &array   = msg.payload<Syncobj_timeline_array>();
		uint64_t               *points  = msg.append<uint64_t>(2);
		uint32_t               *handles = msg.append<uint32_t>(2, 4);

		array.handles       = msg.offset(handles);
		array.points        = msg.offset(points);
		array.count_handles = 2;
		CHECK(msg.finish());
		CHECK(b.check() == Check::OK);

		array.points = 0;
		CHECK(b.check() == (op == Op::SYNCOBJ_QUERY ? Check::BAD_REFERENCE
		                                            : Check::OK));
		array.points        = msg.offset(points);
		array.count_handles = 3;
		CHECK(b.check() == Check::BAD_REFERENCE);
	}

	/* SYNCOBJ_RESET, SYNCOBJ_SIGNAL */
	for (Op const op : { Op::SYNCOBJ_RESET, Op::SYNCOBJ_SIGNAL }) {
		Buffer         b;
		Message        msg(b.bytes, BUFFER_SIZE, op);
		Syncobj_array &array   = msg.payload<Syncobj_array>();
		uint32_t      *handles = msg.append<uint32_t>(1, 4);

		array.handles       = msg.offset(handles);
		array.count_handles = 1;
		CHECK(msg.finish());
		CHECK(b.check() == Check::OK);

		array.count_handles = 2;
		CHECK(b.check() == Check::BAD_REFERENCE);
	}
}


/*
 * 'Message': a message that does not fit must not write behind its buffer,
 * and must not hand out memory that is smaller than what was asked for
 */
static void test_message_overflow()
{
	struct Guarded
	{
		alignas(16) char bytes[96] { };
		char guard[64];
	} g;

	memset(g.guard, 0x5a, sizeof(g.guard));

	auto guard_intact = [&] {
		for (char const c : g.guard)
			if (c != 0x5a) return false;
		return true; };

	/* an array that does not fit: null pointer, nothing written */
	{
		Message     msg(g.bytes, sizeof(g.bytes), Op::VM_BIND);
		Vm_bind    &bind = msg.payload<Vm_bind>();      /* 32 + 24 = 56 */
		Vm_bind_op *ops  = msg.append<Vm_bind_op>(1);   /* 56 + 48 > 96 */

		CHECK(ops == nullptr);

		bind.ops = msg.obj_array(ops, 1);
		CHECK_EQ(bind.ops.array, 0u);

		/* nothing is handed out after the overflow, however small */
		CHECK(msg.append<char>(1, 1) == nullptr);
		CHECK(msg.append_string("") == nullptr);

		CHECK(!msg.finish());
		CHECK(guard_intact());
	}

	/* exactly fitting, then one byte more */
	{
		Message msg(g.bytes, sizeof(g.bytes), Op::SYNCOBJ_TIMELINE_WAIT);
		(void)msg.payload<Syncobj_timeline_wait>();     /* 32 + 48 = 80 */
		uint64_t *points = msg.append<uint64_t>(2);     /* 96 */
		CHECK(points != nullptr);
		if (points) points[1] = ~0ull;
		CHECK(msg.finish());
		CHECK_EQ(msg.size(), 96u);
		CHECK(guard_intact());

		CHECK(msg.append<char>(1, 1) == nullptr);
		CHECK(!msg.finish());
		CHECK(guard_intact());
	}

	/* a string that does not fit by its terminating null */
	{
		Message msg(g.bytes, 32 + 16 + 4, Op::BO_SET_LABEL);
		(void)msg.payload<Bo_set_label>();
		CHECK(msg.append_string("abcd") == nullptr);
		CHECK(!msg.finish());
		CHECK(guard_intact());
	}
	{
		Message msg(g.bytes, 32 + 16 + 5, Op::BO_SET_LABEL);
		(void)msg.payload<Bo_set_label>();
		char const * const text = msg.append_string("abcd");
		CHECK(text != nullptr && !strcmp(text, "abcd"));
		CHECK(msg.finish());
	}

	/* a buffer smaller than the header, no buffer: header and payload are a scratch area */
	{
		Message msg(g.bytes, 16, Op::NOP);
		msg.header().flags = 1;
		CHECK((char *)&msg.header() < g.bytes || (char *)&msg.header() >= g.guard);
		CHECK(!msg.finish());
		CHECK(guard_intact());
	}
	{
		Message msg(nullptr, 4096, Op::GROUP_CREATE);
		Group_create &create = msg.payload<Group_create>();   /* the largest payload */
		create.vm_id = 1;
		create.group_handle = 2;
		CHECK(msg.append<Queue_create>(1) == nullptr);
		CHECK(!msg.finish());
	}

	/* payload without room: usable scratch memory, message fails */
	{
		memset(g.bytes, 0x11, sizeof(g.bytes));

		Message msg(g.bytes, 40, Op::GROUP_CREATE);
		Group_create &create = msg.payload<Group_create>();
		create.tiler_core_mask = ~0ull;
		create.group_handle    = ~0u;
		CHECK(!msg.finish());
		CHECK(guard_intact());
		/* behind the capacity of 40 bytes nothing was touched */
		for (unsigned i = 40; i < sizeof(g.bytes); i++)
			CHECK_EQ(g.bytes[i], 0x11);
	}

	/* element counts whose byte size wraps or exceeds every message */
	{
		Message msg(g.bytes, sizeof(g.bytes), Op::SYNCOBJ_WAIT);
		(void)msg.payload<Syncobj_wait>();
		CHECK(msg.append<uint32_t>(1ull << 40, 4) == nullptr);
		CHECK(!msg.finish());
	}
	{
		Message msg(g.bytes, sizeof(g.bytes), Op::SYNCOBJ_WAIT);
		(void)msg.payload<Syncobj_wait>();
		/* 2^61 * 8 = 2^64: wraps to 0 bytes */
		CHECK(msg.append<uint64_t>(1ull << 61) == nullptr);
		CHECK(!msg.finish());
	}
	{
		Message msg(g.bytes, sizeof(g.bytes), Op::SYNCOBJ_WAIT);
		(void)msg.payload<Syncobj_wait>();
		/* 2^62 * 4 + 4 wraps to 4 bytes */
		CHECK(msg.append<uint32_t>((1ull << 62) + 1, 4) == nullptr);
		CHECK(msg.append<Vm_bind_op>(~0ull) == nullptr);
		CHECK(!msg.finish());
		CHECK(guard_intact());
	}

	/* the capacity is clamped to the largest message */
	{
		std::vector<char> big(2*MAX_MESSAGE_SIZE);
		Message msg(big.data(), big.size(), Op::BO_SYNC);
		(void)msg.payload<Bo_sync>();
		CHECK(msg.append<char>(MAX_MESSAGE_SIZE - 48, 1) != nullptr);
		CHECK(msg.finish());
		CHECK_EQ(msg.size(), (uint64_t)MAX_MESSAGE_SIZE);
		CHECK(msg.append<char>(1, 1) == nullptr);
		CHECK(!msg.finish());
	}
}


/*
 * What an untrusted client may send: truncated messages, sizes and counts
 * that overflow, offsets outside of the message
 */
static void test_untrusted()
{
	/* a valid VM_BIND message, then cut byte by byte: never accepted */
	{
		Buffer      b;
		Message     msg(b.bytes, BUFFER_SIZE, Op::VM_BIND);
		Vm_bind    &bind  = msg.payload<Vm_bind>();
		Vm_bind_op *ops   = msg.append<Vm_bind_op>(2);
		Sync_op    *syncs = msg.append<Sync_op>(2);

		bind.ops     = msg.obj_array(ops, 2);
		ops[0].syncs = msg.obj_array(&syncs[0], 1);
		ops[1].syncs = msg.obj_array(&syncs[1], 1);
		CHECK(msg.finish());
		CHECK(b.check() == Check::OK);

		uint32_t const full = b.header().size;

		/* the client claims the full size but less was copied */
		for (uint32_t avail = 0; avail < full; avail++)
			CHECK(Tunnel::check(b.bytes, avail) == Check::BAD_HEADER);

		/* the client claims less: the references no longer fit */
		for (uint32_t size = 0; size < full; size++) {
			b.header().size = size;
			CHECK(Tunnel::check(b.bytes, BUFFER_SIZE) != Check::OK);
		}
		b.header().size = full;

		/* every reference moved to and beyond the end, and to huge offsets */
		uint64_t * const fields[] = { &bind.ops.array, &ops[0].syncs.array,
		                              &ops[1].syncs.array };

		for (uint64_t * const field : fields) {
			uint64_t const good = *field;

			uint64_t const bad_offsets[] = {
				full, full - 8ull, full + 8ull, 1ull << 31, 1ull << 32,
				(1ull << 32) + good, 1ull << 63, ~0ull, ~0ull - 7, 0, 8, 32 };

			for (uint64_t const bad : bad_offsets) {
				*field = bad;
				CHECK(b.check() == Check::BAD_REFERENCE);
			}
			*field = good;
			CHECK(b.check() == Check::OK);
		}

		/* counts: every value above the real one is refused */
		for (uint32_t const count : { 3u, 4u, 0x100u, 0x10000u, 0x05555556u,
		                              0x80000000u, 0xffffffffu }) {
			bind.ops.count = count;
			CHECK(b.check() == Check::BAD_REFERENCE);
		}
		bind.ops.count = 2;

		for (uint32_t const count : { 2u, 0x1000u, 0x10000001u, 0xffffffffu }) {
			ops[1].syncs.count = count;
			CHECK(b.check() == Check::BAD_REFERENCE);
		}
		ops[1].syncs.count = 1;
		CHECK(b.check() == Check::OK);
	}

	/* unknown header flags */
	{
		Buffer  b;
		Message msg(b.bytes, BUFFER_SIZE, Op::NOP, Header::FLAG_SAMPLE_FLUSH_ID);
		CHECK(msg.finish());
		CHECK(b.check() == Check::OK);

		b.header().flags = 2;
		CHECK(b.check() == Check::BAD_HEADER);
		b.header().flags = 0x80000001u;
		CHECK(b.check() == Check::BAD_HEADER);
	}

	/* handle arrays: count * 4 and count * 8 beyond the message and wrapping */
	{
		Buffer                 b;
		Message                msg(b.bytes, BUFFER_SIZE, Op::SYNCOBJ_TIMELINE_WAIT);
		Syncobj_timeline_wait &wait    = msg.payload<Syncobj_timeline_wait>();
		uint64_t              *points  = msg.append<uint64_t>(4);
		uint32_t              *handles = msg.append<uint32_t>(4, 4);

		wait.points        = msg.offset(points);
		wait.handles       = msg.offset(handles);
		wait.count_handles = 4;
		CHECK(msg.finish());
		CHECK(b.check() == Check::OK);

		/* 5 handles fit behind the handles' offset no more; 5 points would */
		for (uint32_t const count : { 5u, 0x20000000u, 0x20000001u,
		                              0x40000000u, 0x40000004u, 0xffffffffu }) {
			wait.count_handles = count;
			CHECK(b.check() == Check::BAD_REFERENCE);
		}
	}

	/* a result buffer whose size reaches beyond the message */
	{
		Buffer     b;
		Message    msg(b.bytes, BUFFER_SIZE, Op::DEV_QUERY);
		Dev_query &q = msg.payload<Dev_query>();

		q.pointer = msg.offset(msg.append<char>(64));
		CHECK(msg.finish());

		for (uint32_t const size : { 64u, 0u, 1u })
			{ q.size = size; CHECK(b.check() == Check::OK); }

		for (uint32_t const size : { 65u, 4096u, 0x7fffffffu, 0xffffffffu })
			{ q.size = size; CHECK(b.check() == Check::BAD_REFERENCE); }
	}
}


/*
 * Visitor for the fuzz test: every reference of a relocated message must be
 * an address inside of the message behind the payload
 */
struct Inside
{
	uint64_t const base, lo, hi;

	bool ok { true };

	unsigned references { 0 };

	void fail() { ok = false; }

	uint64_t region(uint64_t &field, uint64_t bytes, uint64_t)
	{
		uint64_t const off = field - base;

		references++;

		if (field < base || off < lo || off > hi || bytes > hi - off)
			ok = false;

		return off;
	}

	void string(uint64_t &field) { (void)region(field, 1, 1); }
};


static uint64_t rng_state = 0x9e3779b97f4a7c15ull;

static uint64_t rng()
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return rng_state;
}


/*
 * Random messages: whatever passes the check must relocate to references
 * inside of the message and come back unchanged.
 */
static void test_fuzz()
{
	enum { ROUNDS = 400000, SIZE = 512 };

	unsigned accepted = 0, accepted_with_references = 0, violations = 0,
	         not_restored = 0;

	for (unsigned round = 0; round < ROUNDS; round++) {

		alignas(16) char msg[SIZE];

		/* small numbers, so that offsets, counts and strides often fit */
		for (unsigned i = 0; i < SIZE; i += 4) {
			uint64_t const r = rng();
			uint32_t v = 0;

			switch (r & 7) {
			case 0: case 1: v = 0;                              break;
			case 2: case 3: v = (uint32_t)(r >> 8) % 8;         break;
			case 4: case 5: v = ((uint32_t)(r >> 8) % 64) * 8;  break;
			case 6:         v = (uint32_t)(r >> 8) % SIZE;      break;
			case 7:         v = (uint32_t)(r >> 8);             break;
			}
			memcpy(msg + i, &v, 4);
		}

		Header &h = *(Header *)msg;

		h.magic   = MAGIC;
		h.version = VERSION;
		h.op      = (uint32_t)all_ops[rng() % (sizeof(all_ops)/sizeof(all_ops[0]))];
		h.size    = SIZE - (uint32_t)(rng() % 64);
		h.flags   = (uint32_t)(rng() & 1);

		if (Tunnel::check(msg, SIZE) != Check::OK)
			continue;

		accepted++;

		char original[SIZE];
		memcpy(original, msg, SIZE);

		uint64_t const base = (uint64_t)msg;

		Tunnel::relocate(msg, base);

		Inside inside { .base = base,
		                .lo   = sizeof(Header) + Tunnel::payload_size((Op)h.op),
		                .hi   = h.size };

		/* walks the relocated message like 'unrelocate' does */
		struct Probe
		{
			Inside &inside;
			bool    ok { true };

			void fail() { ok = false; inside.fail(); }

			uint64_t region(uint64_t &field, uint64_t bytes, uint64_t align)
			{
				uint64_t const off = inside.region(field, bytes, align);
				ok = inside.ok;
				return off;
			}

			void string(uint64_t &field) { inside.string(field); ok = inside.ok; }
		} probe { inside };

		Tunnel::visit(msg, probe);

		if (!inside.ok)
			violations++;

		if (inside.references)
			accepted_with_references++;

		Tunnel::unrelocate(msg, base);

		if (memcmp(original, msg, SIZE))
			not_restored++;
	}

	std::printf("fuzz: %u of %u random messages accepted, %u of them with "
	            "references\n", accepted, (unsigned)ROUNDS, accepted_with_references);

	CHECK_EQ(violations,   0u);
	CHECK_EQ(not_restored, 0u);

	/* the test is worthless if nothing with references gets through */
	CHECK(accepted_with_references > 1000);
}


int main()
{
	test_sizes();
	test_header();
	test_dev_query();
	test_vm_bind();
	test_group();
	test_label();
	test_syncobj();
	test_message_overflow();
	test_untrusted();
	test_fuzz();

	return check_result("test_tunnel");
}
