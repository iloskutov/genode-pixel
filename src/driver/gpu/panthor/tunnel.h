/*
 * \brief  Validation and pointer relocation of Gpu-session messages
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * A message (include/gpu/panthor_session.h) carries the argument structure
 * of a DRM ioctl. Its pointer members hold offsets into the message. Before
 * the driver hands the structure to 'drm_ioctl', every reference is checked
 * to lie inside the message behind the payload and is turned into an
 * address ('relocate'), afterwards back into the offset ('unrelocate').
 *
 * The functions must be applied to a private copy of the message: they read
 * counts and strides more than once.
 *
 * No dependency on Genode or Linux, tested on the host (test/).
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _TUNNEL_H_
#define _TUNNEL_H_

#include <gpu/panthor_session.h>

namespace Gpu { namespace Panthor { namespace Tunnel {

	enum class Check { OK, BAD_HEADER, BAD_VERSION, UNKNOWN_OP, BAD_REFERENCE };

	enum : uint32_t { UNKNOWN_PAYLOAD = ~0u };

	/* upper bound of an element size, a multiple of 8 */
	enum : uint32_t { MAX_STRIDE = 1024 };

	/* upper bound of the label of a buffer object, including the null */
	enum : uint32_t { MAX_STRING = 4096 };


	/**
	 * Size of the payload of a request type
	 *
	 * \return UNKNOWN_PAYLOAD for an unknown type
	 */
	static inline uint32_t payload_size(Op op)
	{
		switch (op) {
		case Op::NOP:                     return 0;
		case Op::DEV_QUERY:               return sizeof(Dev_query);
		case Op::VM_CREATE:               return sizeof(Vm_create);
		case Op::VM_DESTROY:              return sizeof(Vm_destroy);
		case Op::VM_BIND:                 return sizeof(Vm_bind);
		case Op::VM_GET_STATE:            return sizeof(Vm_get_state);
		case Op::BO_CREATE:               return sizeof(Bo_create);
		case Op::GROUP_CREATE:            return sizeof(Group_create);
		case Op::GROUP_DESTROY:           return sizeof(Group_destroy);
		case Op::GROUP_SUBMIT:            return sizeof(Group_submit);
		case Op::GROUP_GET_STATE:         return sizeof(Group_get_state);
		case Op::TILER_HEAP_CREATE:       return sizeof(Tiler_heap_create);
		case Op::TILER_HEAP_DESTROY:      return sizeof(Tiler_heap_destroy);
		case Op::BO_SET_LABEL:            return sizeof(Bo_set_label);
		case Op::GEM_CLOSE:               return sizeof(Gem_close);
		case Op::SYNCOBJ_CREATE:          return sizeof(Syncobj_create);
		case Op::SYNCOBJ_DESTROY:         return sizeof(Syncobj_destroy);
		case Op::SYNCOBJ_HANDLE_TO_FD:    return sizeof(Syncobj_handle);
		case Op::SYNCOBJ_FD_TO_HANDLE:    return sizeof(Syncobj_handle);
		case Op::SYNCOBJ_WAIT:            return sizeof(Syncobj_wait);
		case Op::SYNCOBJ_RESET:           return sizeof(Syncobj_array);
		case Op::SYNCOBJ_SIGNAL:          return sizeof(Syncobj_array);
		case Op::SYNCOBJ_TIMELINE_WAIT:   return sizeof(Syncobj_timeline_wait);
		case Op::SYNCOBJ_QUERY:           return sizeof(Syncobj_timeline_array);
		case Op::SYNCOBJ_TRANSFER:        return sizeof(Syncobj_transfer);
		case Op::SYNCOBJ_TIMELINE_SIGNAL: return sizeof(Syncobj_timeline_array);
		case Op::FD_CLOSE:                return sizeof(Fd_close);
		case Op::BO_SYNC:                 return sizeof(Bo_sync);
		case Op::TEST_SLEEP:              return sizeof(Test_sleep);
		case Op::BO_QUERY_INFO:           return sizeof(Bo_query_info);
		}
		return UNKNOWN_PAYLOAD;
	}


	/**
	 * Visitor that checks the references of a message
	 *
	 * A reference is valid if its target lies in [lo, hi) of the message
	 * and is aligned.
	 */
	struct Checker
	{
		char const * const msg;
		uint64_t     const lo;
		uint64_t     const hi;

		bool ok { true };

		void fail() { ok = false; }

		bool _inside(uint64_t off, uint64_t bytes, uint64_t align) const
		{
			return off >= lo && off <= hi && bytes <= hi - off
			    && !(off & (align - 1));
		}

		uint64_t region(uint64_t &field, uint64_t bytes, uint64_t align)
		{
			if (!_inside(field, bytes, align))
				ok = false;

			return field;
		}

		void string(uint64_t &field)
		{
			if (!_inside(field, 1, 1)) {
				ok = false;
				return;
			}

			uint64_t const end = (hi - field > MAX_STRING) ? field + MAX_STRING : hi;

			for (uint64_t i = field; i < end; i++)
				if (!msg[i])
					return;

			ok = false;
		}
	};


	/**
	 * Visitor that turns offsets into addresses, or back
	 */
	struct Shifter
	{
		uint64_t const base;
		bool     const undo;

		bool ok { true };

		void fail() { ok = false; }

		uint64_t region(uint64_t &field, uint64_t, uint64_t)
		{
			if (undo) {
				field -= base;
				return field;
			}

			uint64_t const off = field;
			field += base;
			return off;
		}

		void string(uint64_t &field) { (void)region(field, 0, 1); }
	};


	/**
	 * Visit an object array
	 *
	 * \return offset of the first element in the message, 0 if the array
	 *         is empty or invalid
	 */
	template <typename VISITOR>
	static inline uint64_t visit_obj_array(Obj_array &array, uint32_t min_stride,
	                                       VISITOR &visitor)
	{
		if (!array.count)
			return 0;

		if (array.stride < min_stride || array.stride > MAX_STRIDE
		 || (array.stride & 7)) {
			visitor.fail();
			return 0;
		}

		uint64_t const off =
			visitor.region(array.array, (uint64_t)array.stride*array.count, 8);

		return visitor.ok ? off : 0;
	}


	/**
	 * Visit all references of a message with a known request type
	 */
	template <typename VISITOR>
	static inline void visit(char *msg, VISITOR &visitor)
	{
		Header &header  = *(Header *)msg;
		char   *payload = msg + sizeof(Header);

		auto handles = [&] (uint64_t &field, uint32_t count) {
			if (count)
				visitor.region(field, (uint64_t)count*sizeof(uint32_t), 4); };

		auto points = [&] (uint64_t &field, uint32_t count) {
			if (count)
				visitor.region(field, (uint64_t)count*sizeof(uint64_t), 8); };

		/* object array whose elements hold an array of 'Sync_op' */
		auto nested = [&] (Obj_array &array, uint32_t min_stride,
		                   uint64_t syncs_offset) {

			uint64_t const first = visit_obj_array(array, min_stride, visitor);
			if (!first)
				return;

			for (uint32_t i = 0; i < array.count && visitor.ok; i++) {
				Obj_array &syncs = *(Obj_array *)
					(msg + first + (uint64_t)i*array.stride + syncs_offset);

				(void)visit_obj_array(syncs, sizeof(Sync_op), visitor);
			}
		};

		switch ((Op)header.op) {

		case Op::DEV_QUERY:
			{
				Dev_query &q = *(Dev_query *)payload;
				if (q.pointer)
					visitor.region(q.pointer, q.size, 8);
				break;
			}

		case Op::VM_BIND:
			{
				Vm_bind &b = *(Vm_bind *)payload;
				nested(b.ops, sizeof(Vm_bind_op),
				       __builtin_offsetof(Vm_bind_op, syncs));
				break;
			}

		case Op::GROUP_CREATE:
			{
				Group_create &c = *(Group_create *)payload;
				(void)visit_obj_array(c.queues, sizeof(Queue_create), visitor);
				break;
			}

		case Op::GROUP_SUBMIT:
			{
				Group_submit &s = *(Group_submit *)payload;
				nested(s.queue_submits, sizeof(Queue_submit),
				       __builtin_offsetof(Queue_submit, syncs));
				break;
			}

		case Op::BO_SET_LABEL:
			{
				Bo_set_label &l = *(Bo_set_label *)payload;
				if (l.label)
					visitor.string(l.label);
				break;
			}

		case Op::SYNCOBJ_WAIT:
			{
				Syncobj_wait &w = *(Syncobj_wait *)payload;
				handles(w.handles, w.count_handles);
				break;
			}

		case Op::SYNCOBJ_TIMELINE_WAIT:
			{
				Syncobj_timeline_wait &w = *(Syncobj_timeline_wait *)payload;
				handles(w.handles, w.count_handles);

				/* the DRM core accepts a null pointer: all points are 0 */
				if (w.points)
					points(w.points, w.count_handles);
				break;
			}

		case Op::SYNCOBJ_RESET:
		case Op::SYNCOBJ_SIGNAL:
			{
				Syncobj_array &a = *(Syncobj_array *)payload;
				handles(a.handles, a.count_handles);
				break;
			}

		case Op::SYNCOBJ_QUERY:
			{
				/* the points are the output: the reference is mandatory */
				Syncobj_timeline_array &a = *(Syncobj_timeline_array *)payload;
				handles(a.handles, a.count_handles);
				points (a.points,  a.count_handles);
				break;
			}

		case Op::SYNCOBJ_TIMELINE_SIGNAL:
			{
				Syncobj_timeline_array &a = *(Syncobj_timeline_array *)payload;
				handles(a.handles, a.count_handles);

				/* a null pointer signals point 0 of every syncobj */
				if (a.points)
					points(a.points, a.count_handles);
				break;
			}

		case Op::BO_SYNC:
			{
				Bo_sync &s = *(Bo_sync *)payload;
				(void)visit_obj_array(s.ops, sizeof(Bo_sync_op), visitor);
				break;
			}

		/* no references */
		case Op::NOP:
		case Op::VM_CREATE:
		case Op::VM_DESTROY:
		case Op::VM_GET_STATE:
		case Op::BO_CREATE:
		case Op::GROUP_DESTROY:
		case Op::GROUP_GET_STATE:
		case Op::TILER_HEAP_CREATE:
		case Op::TILER_HEAP_DESTROY:
		case Op::GEM_CLOSE:
		case Op::SYNCOBJ_CREATE:
		case Op::SYNCOBJ_DESTROY:
		case Op::SYNCOBJ_HANDLE_TO_FD:
		case Op::SYNCOBJ_FD_TO_HANDLE:
		case Op::SYNCOBJ_TRANSFER:
		case Op::FD_CLOSE:
		case Op::TEST_SLEEP:
		case Op::BO_QUERY_INFO:
			break;
		}
	}


	/**
	 * Check a message
	 *
	 * \param msg    private copy of the message
	 * \param avail  number of bytes at 'msg'
	 */
	static inline Check check(char *msg, uint64_t avail)
	{
		if (avail < sizeof(Header))
			return Check::BAD_HEADER;

		Header const &header = *(Header const *)msg;

		if (header.magic != MAGIC)
			return Check::BAD_HEADER;

		if ((header.version >> 16) != VERSION_MAJOR)
			return Check::BAD_VERSION;

		if (header.size < sizeof(Header) || header.size > avail
		 || header.size > MAX_MESSAGE_SIZE)
			return Check::BAD_HEADER;

		uint32_t const payload = payload_size((Op)header.op);

		if (payload == UNKNOWN_PAYLOAD)
			return Check::UNKNOWN_OP;

		if (header.size - sizeof(Header) < payload)
			return Check::BAD_HEADER;

		if (header.flags & ~(uint32_t)Header::KNOWN_FLAGS)
			return Check::BAD_HEADER;

		Checker checker { .msg = msg,
		                  .lo  = sizeof(Header) + payload,
		                  .hi  = header.size };

		visit(msg, checker);

		return checker.ok ? Check::OK : Check::BAD_REFERENCE;
	}


	/**
	 * Turn the offsets of a checked message into addresses
	 *
	 * \param base  address the offsets refer to, i.e., of the message
	 */
	static inline void relocate(char *msg, uint64_t base)
	{
		Shifter shifter { .base = base, .undo = false };
		visit(msg, shifter);
	}


	/**
	 * Revert 'relocate'
	 */
	static inline void unrelocate(char *msg, uint64_t base)
	{
		Shifter shifter { .base = base, .undo = true };
		visit(msg, shifter);
	}

} /* namespace Tunnel */ } /* namespace Panthor */ } /* namespace Gpu */

#endif /* _TUNNEL_H_ */
