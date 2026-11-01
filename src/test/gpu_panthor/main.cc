/*
 * \brief  Test of the Gpu session of the panthor GPU driver, without Mesa
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Talks the protocol of include/gpu/panthor_session.h directly and walks
 * through what a Mesa back end needs, one step at a time. A log line is
 * printed BEFORE each step, so that the last line of a log names the step
 * that hung or took the driver down.
 *
 * The steps: session, exchange buffer, malformed messages, a blocking request
 * (pending -> completion signal), the requests of the DRM core (syncobjs, sync
 * files, descriptors), then the GPU: device queries, VM, buffer object
 * (mapping, pattern), VM bind (synchronous and asynchronous), syncobjs (wait
 * with time-out, timeline, transfer, sync file), tiler heap, group with one
 * queue, group state, the submission of a command stream that consists of two
 * NOP instructions, destruction of everything, cached buffer objects.
 *
 * The test ends with "gpu_panthor: DONE" or "gpu_panthor: FAILED".
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/attached_dataspace.h>
#include <base/attached_ram_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <cpu/cache.h>
#include <gpu/panthor_session.h>
#include <gpu_session/connection.h>
#include <timer_session/connection.h>
#include <util/reconstructible.h>

namespace Test {

	using namespace Genode;
	using namespace Gpu::Panthor;

	using Genode::uint8_t;
	using Genode::uint32_t;
	using Genode::int32_t;
	using Genode::uint64_t;
	using Genode::int64_t;

	struct Main;
}


struct Test::Main
{
	struct Failed : Exception { };

	enum { EXCHANGE_BUFFER_SIZE = 16*1024 };

	enum : uint64_t {
		BO_SIZE  = 64*1024,
		BO_VA    = 0x1000000,    /* synchronous mapping */
		BO_VA_2  = 0x2000000,    /* asynchronous mapping */
	};

	Env &_env;

	Timer::Connection _timer { _env };

	Gpu::Connection _gpu { _env };

	Info const &_info = *_gpu.attached_info<Info>();

	unsigned _signals = 0;

	void _handle_completion() { _signals++; }

	Io_signal_handler<Main> _completion_handler {
		_env.ep(), *this, &Main::_handle_completion };

	Gpu::Vram_id const _exchange_id { .value = 1 };

	Constructible<Attached_dataspace> _exchange { };

	unsigned _step = 0;

	/* true if the last request was pending when 'execute' returned */
	bool _was_pending = false;

	void _begin(auto &&... args)
	{
		log("gpu_panthor: step ", ++_step, ": ", args...);
	}

	[[noreturn]] void _fail(auto &&... args)
	{
		error("gpu_panthor: step ", _step, ": ", args...);
		throw Failed();
	}

	void *_buffer() { return _exchange->local_addr<void>(); }

	/**
	 * Result of 'Message::append': a null pointer means "does not fit"
	 */
	template <typename T>
	T *_need(T *ptr)
	{
		if (!ptr)
			_fail("message does not fit into the exchange buffer");

		return ptr;
	}

	Message _message(Op op, uint32_t flags = 0)
	{
		return Message(_buffer(), EXCHANGE_BUFFER_SIZE, op, flags);
	}

	Header volatile &_header() { return *(Header volatile *)_buffer(); }

	/**
	 * Execute the message at the start of the exchange buffer
	 *
	 * \return result of the request (0 or negative Linux errno)
	 */
	int32_t _execute_raw()
	{
		Gpu::Sequence_number seqno { };

		try {
			seqno = _gpu.execute(_exchange_id, 0);
		} catch (Gpu::Session::Invalid_state) {
			_fail("execute threw Invalid_state");
		}

		_was_pending = (_header().state == Header::PENDING);

		while (_header().state != Header::DONE)
			_env.ep().wait_and_dispatch_one_io_signal();

		if (!_gpu.complete(seqno))
			_fail("response is there, but 'complete' denies it");

		return _header().result;
	}

	int32_t _execute(Message &msg)
	{
		if (!msg.finish())
			_fail("message does not fit into the exchange buffer");

		return _execute_raw();
	}

	void _expect(Message &msg, int32_t expected, char const *what)
	{
		int32_t const result = _execute(msg);

		if (result != expected)
			_fail(what, ": result ", result, ", expected ", expected);
	}

	void _session();
	void _malformed();
	void _blocking();
	void _drm_core();
	void _device();
	void _cached_bo(uint32_t vm_id);

	Main(Env &env) : _env(env)
	{
		try {
			_session();
			_malformed();
			_blocking();
			_drm_core();
			_device();

			log("gpu_panthor: DONE");

		} catch (Failed) {
			error("gpu_panthor: FAILED");
		}
	}
};


void Test::Main::_session()
{
	_begin("session info");

	log("  protocol ", _info.version >> 16, ".", _info.version & 0xffff,
	    ", device ", _info.device, ", drm_core ", _info.drm_core,
	    ", DRM driver ", _info.drm_version_major,
	    ".", _info.drm_version_minor, ".", _info.drm_version_patch,
	    ", buffer objects ", _info.bo_cached ? "cached on request" : "uncached");

	if (_info.magic != MAGIC || (_info.version >> 16) != VERSION_MAJOR)
		_fail("info dataspace has no valid header");

	log("  gpu_id ", Hex(_info.gpu.gpu_id), " csf_id ", Hex(_info.gpu.csf_id),
	    " shader_present ", Hex(_info.gpu.shader_present),
	    " tiler_present ", Hex(_info.gpu.tiler_present),
	    " mmu_features ", Hex(_info.gpu.mmu_features),
	    " csg slots ", _info.csif.csg_slot_count,
	    " cs slots ", _info.csif.cs_slot_count);

	_begin("exchange buffer (alloc_vram) and completion signal");

	_gpu.completion_sigh(_completion_handler);

	Dataspace_capability const ds =
		_gpu.alloc_vram(_exchange_id, EXCHANGE_BUFFER_SIZE);

	if (!ds.valid())
		_fail("alloc_vram returned no dataspace");

	_exchange.construct(_env.rm(), ds);

	_begin("NOP");
	{
		Message msg = _message(Op::NOP);
		_expect(msg, 0, "NOP");
		log("  LATEST_FLUSH_ID ", _header().flush_id);
	}
}


void Test::Main::_malformed()
{
	_begin("message with a wrong magic value");
	{
		Message msg = _message(Op::NOP);
		msg.header().magic = 0x12345678;
		_expect(msg, -LX_EPROTO, "wrong magic");
	}

	_begin("message of another major protocol version");
	{
		Message msg = _message(Op::NOP);
		msg.header().version = (VERSION_MAJOR + 1) << 16;
		_expect(msg, -LX_EPROTO, "wrong version");
	}

	_begin("unknown request type");
	{
		Message msg = _message((Op)0x7fff);
		_expect(msg, -LX_ENOSYS, "unknown request type");
	}

	_begin("payload shorter than the request type needs");
	{
		Message msg = _message(Op::VM_BIND);
		_expect(msg, -LX_EPROTO, "short payload");
	}

	_begin("reference that points behind the message");
	{
		Message    msg   = _message(Op::DEV_QUERY);
		Dev_query &query = msg.payload<Dev_query>();

		query.type    = DEV_QUERY_GPU_INFO;
		query.size    = sizeof(Gpu_info);
		query.pointer = sizeof(Header) + sizeof(Dev_query);

		_expect(msg, -LX_EPROTO, "reference behind the message");
	}

	_begin("reference that points into the payload");
	{
		Message    msg   = _message(Op::DEV_QUERY);
		Dev_query &query = msg.payload<Dev_query>();

		(void)_need(msg.append<Gpu_info>(1));

		query.type    = DEV_QUERY_GPU_INFO;
		query.size    = sizeof(Gpu_info);
		query.pointer = sizeof(Header);

		_expect(msg, -LX_EPROTO, "reference into the payload");
	}

	_begin("object array with more elements than the message holds");
	{
		Message     msg  = _message(Op::VM_BIND);
		Vm_bind    &bind = msg.payload<Vm_bind>();
		Vm_bind_op *ops  = _need(msg.append<Vm_bind_op>(2));

		bind.ops       = msg.obj_array(ops, 2);
		bind.ops.count = 1000;

		_expect(msg, -LX_EPROTO, "oversized object array");
	}

	_begin("nested object array that points outside of the message");
	{
		Message     msg  = _message(Op::VM_BIND);
		Vm_bind    &bind = msg.payload<Vm_bind>();
		Vm_bind_op *ops  = _need(msg.append<Vm_bind_op>(1));

		bind.ops     = msg.obj_array(ops, 1);
		ops[0].syncs = { .stride = sizeof(Sync_op), .count = 4,
		                 .array  = msg.size() };

		_expect(msg, -LX_EPROTO, "nested array outside of the message");
	}

	_begin("execute with an unaligned offset and with an unknown buffer");
	{
		bool refused = false;
		try { _gpu.execute(_exchange_id, 4); }
		catch (Gpu::Session::Invalid_state) { refused = true; }

		if (!refused)
			_fail("unaligned offset accepted");

		refused = false;
		try { _gpu.execute(Gpu::Vram_id { .value = 77 }, 0); }
		catch (Gpu::Session::Invalid_state) { refused = true; }

		if (!refused)
			_fail("unknown exchange buffer accepted");
	}

	_begin("map_cpu of an unknown buffer object");
	{
		if (_gpu.map_cpu(Gpu::Vram_id { .value = 0x7fffffff },
		                 Gpu::Mapping_attributes::rw()).valid())
			_fail("got a dataspace for an unknown handle");
	}
}


void Test::Main::_blocking()
{
	enum { SLEEP_MS = 300 };

	_begin("TEST_SLEEP ", (unsigned)SLEEP_MS, " ms: a request that blocks the driver's task");

	Message     msg   = _message(Op::TEST_SLEEP);
	Test_sleep &sleep = msg.payload<Test_sleep>();

	sleep.ms = SLEEP_MS;

	unsigned const signals_before = _signals;
	uint64_t const start_ms       = _timer.elapsed_ms();

	int32_t const result = _execute(msg);

	uint64_t const ms = _timer.elapsed_ms() - start_ms;

	if (result == -LX_ENOSYS) {
		log("  not served (driver without test_ops=\"yes\"), skipped");
		return;
	}

	if (result)
		_fail("TEST_SLEEP: result ", result);

	log("  returned after ", ms, " ms, was pending: ", _was_pending,
	    ", completion signals: ", _signals - signals_before);

	if (!_was_pending || _signals == signals_before || ms + 20 < SLEEP_MS)
		_fail("the request did not go through the pending state");

	/*
	 * A second session is closed while its request blocks: the driver has
	 * to interrupt the wait, and the first session must still be served.
	 */
	enum { LONG_SLEEP_MS = 8000 };

	_begin("second session, closed while its TEST_SLEEP of ",
	       (unsigned)LONG_SLEEP_MS, " ms is pending");
	{
		Gpu::Vram_id const id { .value = 5 };

		uint64_t const start_ms = _timer.elapsed_ms();

		{
			Gpu::Connection second { _env };

			Dataspace_capability const ds = second.alloc_vram(id, 4096);
			if (!ds.valid())
				_fail("alloc_vram of the second session returned no dataspace");

			Attached_dataspace buffer { _env.rm(), ds };

			Message     msg(buffer.local_addr<void>(), 4096, Op::TEST_SLEEP);
			Test_sleep &sleep = msg.payload<Test_sleep>();

			sleep.ms = LONG_SLEEP_MS;
			if (!msg.finish())
				_fail("message does not fit");

			Gpu::Sequence_number const seqno = second.execute(id, 0);

			if (msg.header().state != Header::PENDING || second.complete(seqno))
				_fail("the request of the second session is not pending");

			/* a second message while one is in flight is refused */
			bool refused = false;
			try { second.execute(id, 0); }
			catch (Gpu::Session::Invalid_state) { refused = true; }

			if (!refused)
				_fail("second message accepted while the first one is pending");
		}

		Message nop = _message(Op::NOP);
		_expect(nop, 0, "NOP of the first session");

		uint64_t const ms = _timer.elapsed_ms() - start_ms;

		log("  second session closed after ", ms, " ms, first session answers");

		if (ms >= LONG_SLEEP_MS)
			_fail("closing the session waited for the blocked request");
	}
}


/*
 * Requests of the DRM core: they need a DRM file but no GPU
 */
void Test::Main::_drm_core()
{
	uint32_t syncobj = 0, timeline = 0;

	auto create = [&] (char const *what) {

		Message         msg = _message(Op::SYNCOBJ_CREATE);
		Syncobj_create &c   = msg.payload<Syncobj_create>();

		_expect(msg, 0, what);
		log("  handle ", c.handle);
		return (uint32_t)c.handle;
	};

	auto destroy = [&] (uint32_t handle) {

		Message          msg = _message(Op::SYNCOBJ_DESTROY);
		Syncobj_destroy &d   = msg.payload<Syncobj_destroy>();

		d.handle = handle;
		_expect(msg, 0, "SYNCOBJ_DESTROY");
	};

	auto wait = [&] (uint32_t handle, int64_t timeout_ns, uint32_t flags) {

		Message       msg     = _message(Op::SYNCOBJ_WAIT);
		Syncobj_wait &w       = msg.payload<Syncobj_wait>();
		uint32_t     &handles = *_need(msg.append<uint32_t>(1, 4));

		handles         = handle;
		w.handles       = msg.offset(&handles);
		w.count_handles = 1;
		w.timeout_nsec  = timeout_ns;
		w.flags         = flags;

		uint64_t const start_ms = _timer.elapsed_ms();
		int32_t  const result   = _execute(msg);

		log("  result ", result, " after ", _timer.elapsed_ms() - start_ms,
		    " ms, was pending: ", _was_pending);

		if (w.timeout_nsec != timeout_ns)
			_fail("the relative time-out came back changed");

		return result;
	};

	auto array_op = [&] (Op op, uint32_t handle, char const *what) {

		Message        msg     = _message(op);
		Syncobj_array &array   = msg.payload<Syncobj_array>();
		uint32_t      &handles = *_need(msg.append<uint32_t>(1, 4));

		handles             = handle;
		array.handles       = msg.offset(&handles);
		array.count_handles = 1;

		_expect(msg, 0, what);
	};

	auto handle_fd = [&] (Op op, uint32_t handle, uint32_t flags, int32_t fd,
	                      char const *what) {

		Message         msg = _message(op);
		Syncobj_handle &h   = msg.payload<Syncobj_handle>();

		h.handle = handle;
		h.flags  = flags;
		h.fd     = fd;

		_expect(msg, 0, what);
		return h;
	};

	auto fd_close = [&] (int32_t fd) {

		Message   msg = _message(Op::FD_CLOSE);
		Fd_close &c   = msg.payload<Fd_close>();

		c.fd = fd;
		return _execute(msg);
	};

	_begin("DRM core: SYNCOBJ_CREATE (binary, unsignaled)");
	syncobj = create("SYNCOBJ_CREATE");

	_begin("DRM core: SYNCOBJ_WAIT, unsignaled, time-out 200 ms: must end with ETIME");
	{
		int32_t const result =
			wait(syncobj, 200ll*1000*1000, SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT);

		if (result != -LX_ETIME)
			_fail("result ", result, ", expected ", -LX_ETIME);

		if (!_was_pending)
			_fail("the wait did not block");
	}

	_begin("DRM core: SYNCOBJ_SIGNAL, then SYNCOBJ_WAIT (polling)");
	{
		array_op(Op::SYNCOBJ_SIGNAL, syncobj, "SYNCOBJ_SIGNAL");

		if (wait(syncobj, 0, 0))
			_fail("signaled syncobj does not count as signaled");
	}

	int32_t sync_file = -1;

	_begin("DRM core: SYNCOBJ_HANDLE_TO_FD, the fence as sync file");
	{
		sync_file = handle_fd(Op::SYNCOBJ_HANDLE_TO_FD, syncobj,
		                      SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE, -1,
		                      "SYNCOBJ_HANDLE_TO_FD").fd;

		log("  descriptor ", sync_file, " (of the session's table in the driver)");

		if (sync_file < 0)
			_fail("no descriptor");
	}

	_begin("DRM core: SYNCOBJ_RESET, wait must end with ETIME again");
	{
		array_op(Op::SYNCOBJ_RESET, syncobj, "SYNCOBJ_RESET");

		if (wait(syncobj, 0, SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT) != -LX_ETIME)
			_fail("reset syncobj still counts as signaled");
	}

	_begin("DRM core: SYNCOBJ_FD_TO_HANDLE, sync file into the syncobj, wait");
	{
		(void)handle_fd(Op::SYNCOBJ_FD_TO_HANDLE, syncobj,
		                SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE, sync_file,
		                "SYNCOBJ_FD_TO_HANDLE");

		if (wait(syncobj, 0, 0))
			_fail("the imported fence does not count as signaled");
	}

	_begin("DRM core: FD_CLOSE, twice: the second one must end with EBADF");
	{
		if (int32_t const result = fd_close(sync_file))
			_fail("FD_CLOSE: result ", result);

		if (int32_t const result = fd_close(sync_file); result != -LX_EBADF)
			_fail("second FD_CLOSE: result ", result, ", expected ", -LX_EBADF);
	}

	_begin("DRM core: syncobj as descriptor and back as a second handle");
	{
		int32_t const fd = handle_fd(Op::SYNCOBJ_HANDLE_TO_FD, syncobj, 0, -1,
		                             "SYNCOBJ_HANDLE_TO_FD").fd;

		uint32_t const second = handle_fd(Op::SYNCOBJ_FD_TO_HANDLE, 0, 0, fd,
		                                  "SYNCOBJ_FD_TO_HANDLE").handle;

		log("  descriptor ", fd, ", second handle ", second);

		if (second == syncobj || !second)
			_fail("no second handle");

		/* the second handle names the same, signaled, syncobj */
		if (wait(second, 0, 0))
			_fail("the second handle is not signaled");

		if (int32_t const result = fd_close(fd))
			_fail("FD_CLOSE: result ", result);

		destroy(second);
	}

	_begin("DRM core: timeline: SYNCOBJ_CREATE, SYNCOBJ_TIMELINE_SIGNAL of point 3");
	{
		timeline = create("SYNCOBJ_CREATE");

		Message                 msg     = _message(Op::SYNCOBJ_TIMELINE_SIGNAL);
		Syncobj_timeline_array &array   = msg.payload<Syncobj_timeline_array>();
		uint64_t               &points  = *_need(msg.append<uint64_t>(1));
		uint32_t               &handles = *_need(msg.append<uint32_t>(1, 4));

		points              = 3;
		handles             = timeline;
		array.handles       = msg.offset(&handles);
		array.points        = msg.offset(&points);
		array.count_handles = 1;

		_expect(msg, 0, "SYNCOBJ_TIMELINE_SIGNAL");
	}

	_begin("DRM core: SYNCOBJ_QUERY of the timeline: must be 3");
	{
		Message                 msg     = _message(Op::SYNCOBJ_QUERY);
		Syncobj_timeline_array &array   = msg.payload<Syncobj_timeline_array>();
		uint64_t               &points  = *_need(msg.append<uint64_t>(1));
		uint32_t               &handles = *_need(msg.append<uint32_t>(1, 4));

		handles             = timeline;
		array.handles       = msg.offset(&handles);
		array.points        = msg.offset(&points);
		array.count_handles = 1;

		_expect(msg, 0, "SYNCOBJ_QUERY");

		log("  last signaled point ", points);

		if (points != 3)
			_fail("the timeline is at point ", points);
	}

	auto timeline_wait = [&] (uint64_t point, int64_t timeout_ns) {

		Message                msg     = _message(Op::SYNCOBJ_TIMELINE_WAIT);
		Syncobj_timeline_wait &w       = msg.payload<Syncobj_timeline_wait>();
		uint64_t              &points  = *_need(msg.append<uint64_t>(1));
		uint32_t              &handles = *_need(msg.append<uint32_t>(1, 4));

		points          = point;
		handles         = timeline;
		w.handles       = msg.offset(&handles);
		w.points        = msg.offset(&points);
		w.count_handles = 1;
		w.timeout_nsec  = timeout_ns;
		w.flags         = SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;

		int32_t const result = _execute(msg);

		log("  point ", point, ": result ", result, ", was pending: ", _was_pending);
		return result;
	};

	_begin("DRM core: SYNCOBJ_TIMELINE_WAIT for point 3 (0) and point 5 (ETIME after 100 ms)");
	{
		if (timeline_wait(3, 0))
			_fail("point 3 does not count as signaled");

		if (timeline_wait(5, 100ll*1000*1000) != -LX_ETIME)
			_fail("point 5 does not time out");
	}

	_begin("DRM core: SYNCOBJ_TRANSFER, point 3 of the timeline to the reset syncobj");
	{
		array_op(Op::SYNCOBJ_RESET, syncobj, "SYNCOBJ_RESET");

		Message           msg      = _message(Op::SYNCOBJ_TRANSFER);
		Syncobj_transfer &transfer = msg.payload<Syncobj_transfer>();

		transfer.src_handle = timeline;
		transfer.src_point  = 3;
		transfer.dst_handle = syncobj;

		_expect(msg, 0, "SYNCOBJ_TRANSFER");

		if (wait(syncobj, 0, 0))
			_fail("the transferred fence does not count as signaled");
	}

	/*
	 * A session that is closed with a syncobj, an open descriptor and a
	 * blocked wait
	 */
	enum { LONG_WAIT_MS = 8000 };

	_begin("DRM core: second session, closed while its SYNCOBJ_WAIT of ",
	       (unsigned)LONG_WAIT_MS, " ms is pending");
	{
		Gpu::Vram_id const id { .value = 6 };

		uint64_t const start_ms = _timer.elapsed_ms();

		{
			Gpu::Connection second { _env };

			Dataspace_capability const ds = second.alloc_vram(id, 4096);
			if (!ds.valid())
				_fail("alloc_vram of the second session returned no dataspace");

			Attached_dataspace buffer { _env.rm(), ds };

			void * const base = buffer.local_addr<void>();

			auto run = [&] (Message &msg) {

				if (!msg.finish())
					_fail("message does not fit");

				(void)second.execute(id, 0);
				return msg.header().state;
			};

			uint32_t handle = 0;
			{
				Message         msg(base, 4096, Op::SYNCOBJ_CREATE);
				Syncobj_create &c = msg.payload<Syncobj_create>();

				if (run(msg) != Header::DONE || msg.header().result)
					_fail("SYNCOBJ_CREATE of the second session failed");

				handle = c.handle;
			}
			{
				/* a descriptor that is never closed by the client */
				Message         msg(base, 4096, Op::SYNCOBJ_HANDLE_TO_FD);
				Syncobj_handle &h = msg.payload<Syncobj_handle>();

				h.handle = handle;
				h.fd     = -1;

				if (run(msg) != Header::DONE || msg.header().result)
					_fail("SYNCOBJ_HANDLE_TO_FD of the second session failed");
			}
			{
				Message       msg(base, 4096, Op::SYNCOBJ_WAIT);
				Syncobj_wait &w       = msg.payload<Syncobj_wait>();
				uint32_t     &handles = *_need(msg.append<uint32_t>(1, 4));

				handles         = handle;
				w.handles       = msg.offset(&handles);
				w.count_handles = 1;
				w.timeout_nsec  = (int64_t)LONG_WAIT_MS*1000*1000;
				w.flags         = SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;

				if (run(msg) != Header::PENDING)
					_fail("the wait of the second session is not pending");
			}
		}

		Message nop = _message(Op::NOP);
		_expect(nop, 0, "NOP of the first session");

		uint64_t const ms = _timer.elapsed_ms() - start_ms;

		log("  second session with a blocked wait closed after ", ms,
		    " ms, first session answers");

		if (ms >= LONG_WAIT_MS)
			_fail("closing the session waited for the blocked request");
	}

	_begin("DRM core: SYNCOBJ_DESTROY of both syncobjs");
	destroy(syncobj);
	destroy(timeline);
}


void Test::Main::_device()
{
	/*
	 * Device queries
	 */

	_begin("DEV_QUERY GPU_INFO (size, then data)");
	{
		Message    msg   = _message(Op::DEV_QUERY);
		Dev_query &query = msg.payload<Dev_query>();

		query.type = DEV_QUERY_GPU_INFO;
		_expect(msg, 0, "size query");
		log("  the kernel's drm_panthor_gpu_info has ", query.size,
		    " bytes, the protocol's ", sizeof(Gpu_info));
	}
	{
		Message    msg   = _message(Op::DEV_QUERY);
		Dev_query &query = msg.payload<Dev_query>();
		Gpu_info  &info  = *_need(msg.append<Gpu_info>(1));

		query.type    = DEV_QUERY_GPU_INFO;
		query.size    = sizeof(Gpu_info);
		query.pointer = msg.offset(&info);

		_expect(msg, 0, "DEV_QUERY GPU_INFO");

		log("  gpu_id ", Hex(info.gpu_id), " l2_features ", Hex(info.l2_features),
		    " tiler_features ", Hex(info.tiler_features),
		    " mem_features ", Hex(info.mem_features),
		    " as_present ", Hex(info.as_present),
		    " shader_present ", Hex(info.shader_present));

		if (info.gpu_id != _info.gpu.gpu_id
		 || info.shader_present != _info.gpu.shader_present)
			_fail("GPU_INFO differs from the info dataspace");
	}

	_begin("DEV_QUERY CSIF_INFO");
	{
		Message    msg   = _message(Op::DEV_QUERY);
		Dev_query &query = msg.payload<Dev_query>();
		Csif_info &info  = *_need(msg.append<Csif_info>(1));

		query.type    = DEV_QUERY_CSIF_INFO;
		query.size    = sizeof(Csif_info);
		query.pointer = msg.offset(&info);

		_expect(msg, 0, "DEV_QUERY CSIF_INFO");

		log("  csg_slot_count ", info.csg_slot_count,
		    " cs_slot_count ", info.cs_slot_count,
		    " cs_reg_count ", info.cs_reg_count,
		    " scoreboard_slot_count ", info.scoreboard_slot_count,
		    " unpreserved_cs_reg_count ", info.unpreserved_cs_reg_count);
	}

	_begin("DEV_QUERY TIMESTAMP_INFO");
	{
		Message         msg   = _message(Op::DEV_QUERY);
		Dev_query      &query = msg.payload<Dev_query>();
		Timestamp_info &info  = *_need(msg.append<Timestamp_info>(1));

		query.type    = DEV_QUERY_TIMESTAMP_INFO;
		query.size    = sizeof(Timestamp_info);
		query.pointer = msg.offset(&info);

		_expect(msg, 0, "DEV_QUERY TIMESTAMP_INFO");

		log("  timestamp_frequency ", info.timestamp_frequency,
		    " current_timestamp ", info.current_timestamp,
		    " timestamp_offset ", info.timestamp_offset);
	}

	_begin("DEV_QUERY GROUP_PRIORITIES_INFO");
	{
		Message                msg   = _message(Op::DEV_QUERY);
		Dev_query             &query = msg.payload<Dev_query>();
		Group_priorities_info &info  = *_need(msg.append<Group_priorities_info>(1));

		query.type    = DEV_QUERY_GROUP_PRIORITIES_INFO;
		query.size    = sizeof(Group_priorities_info);
		query.pointer = msg.offset(&info);

		_expect(msg, 0, "DEV_QUERY GROUP_PRIORITIES_INFO");

		log("  allowed_mask ", Hex(info.allowed_mask));
	}

	/*
	 * VM. The user range is given as Mesa does: the lower half of the
	 * GPU's address space.
	 */

	uint32_t vm_id = 0;

	_begin("VM_CREATE");
	{
		unsigned const va_bits = _info.gpu.mmu_features & 0xff;

		Message    msg = _message(Op::VM_CREATE);
		Vm_create &vm  = msg.payload<Vm_create>();

		vm.user_va_range = 1ull << (va_bits - 1);

		_expect(msg, 0, "VM_CREATE");

		vm_id = vm.id;
		log("  vm ", vm_id, ", ", va_bits, " address bits, user range ",
		    Hex(vm.user_va_range));
	}

	_begin("VM_GET_STATE");
	{
		Message       msg   = _message(Op::VM_GET_STATE);
		Vm_get_state &state = msg.payload<Vm_get_state>();

		state.vm_id = vm_id;
		_expect(msg, 0, "VM_GET_STATE");

		log("  state ", state.state, " (0: usable)");

		if (state.state != VM_STATE_USABLE)
			_fail("the VM is not usable");
	}

	/*
	 * Buffer object
	 */

	uint32_t bo_handle = 0;

	_begin("BO_CREATE ", Hex((uint64_t)BO_SIZE), " bytes, exclusive to the VM");
	{
		Message    msg = _message(Op::BO_CREATE);
		Bo_create &bo  = msg.payload<Bo_create>();

		bo.size            = BO_SIZE;
		bo.exclusive_vm_id = vm_id;

		_expect(msg, 0, "BO_CREATE");

		bo_handle = bo.handle;
		log("  handle ", bo_handle, ", size ", Hex(bo.size));
	}

	Constructible<Attached_dataspace> bo { };

	_begin("map_cpu of the buffer object and attach");
	{
		Dataspace_capability const ds =
			_gpu.map_cpu(Gpu::Vram_id { .value = bo_handle },
			             Gpu::Mapping_attributes::rw());

		if (!ds.valid())
			_fail("map_cpu returned no dataspace");

		bo.construct(_env.rm(), ds);

		log("  ", Hex(bo->size()), " bytes at ", bo->local_addr<void>());

		if (bo->size() < BO_SIZE)
			_fail("dataspace smaller than the buffer object");
	}

	_begin("write a pattern to the buffer object and read it back");
	{
		uint32_t volatile * const words = bo->local_addr<uint32_t volatile>();
		unsigned const            count = BO_SIZE/sizeof(uint32_t);

		auto pattern = [] (unsigned i) { return 0xa5a50000u ^ (i*0x10001u); };

		for (unsigned i = 0; i < count; i++)
			words[i] = pattern(i);

		for (unsigned i = 0; i < count; i++)
			if (words[i] != pattern(i))
				_fail("word ", i, " reads ", Hex(words[i]),
				      ", written ", Hex(pattern(i)));

		log("  ", count, " words match");
	}

	_begin("BO_SET_LABEL");
	{
		Message       msg   = _message(Op::BO_SET_LABEL);
		Bo_set_label &label = msg.payload<Bo_set_label>();

		label.handle = bo_handle;
		label.label  = msg.offset(_need(msg.append_string("gpu_panthor test buffer")));

		_expect(msg, 0, "BO_SET_LABEL");
	}

	_begin("VM_BIND, synchronous: map the buffer object at ", Hex((uint64_t)BO_VA));
	{
		Message     msg  = _message(Op::VM_BIND);
		Vm_bind    &bind = msg.payload<Vm_bind>();
		Vm_bind_op &op   = *_need(msg.append<Vm_bind_op>(1));

		bind.vm_id = vm_id;
		bind.ops   = msg.obj_array(&op, 1);

		op.flags     = VM_BIND_OP_TYPE_MAP | VM_BIND_OP_MAP_NOEXEC;
		op.bo_handle = bo_handle;
		op.va        = BO_VA;
		op.size      = BO_SIZE;

		_expect(msg, 0, "VM_BIND map");
	}

	/*
	 * Syncobjs
	 */

	uint32_t syncobj = 0, timeline = 0;

	_begin("SYNCOBJ_CREATE (binary, unsignaled)");
	{
		Message         msg    = _message(Op::SYNCOBJ_CREATE);
		Syncobj_create &create = msg.payload<Syncobj_create>();

		_expect(msg, 0, "SYNCOBJ_CREATE");

		syncobj = create.handle;
		log("  handle ", syncobj);
	}

	auto wait = [&] (uint32_t handle, int64_t timeout_ns, uint32_t flags) {

		Message       msg     = _message(Op::SYNCOBJ_WAIT);
		Syncobj_wait &w       = msg.payload<Syncobj_wait>();
		uint32_t     &handles = *_need(msg.append<uint32_t>(1, 4));

		handles         = handle;
		w.handles       = msg.offset(&handles);
		w.count_handles = 1;
		w.timeout_nsec  = timeout_ns;
		w.flags         = flags;

		uint64_t const start_ms = _timer.elapsed_ms();
		int32_t  const result   = _execute(msg);

		log("  result ", result, " after ", _timer.elapsed_ms() - start_ms,
		    " ms, was pending: ", _was_pending);

		return result;
	};

	_begin("SYNCOBJ_WAIT on the unsignaled syncobj, time-out 200 ms: must end with ETIME");
	{
		int32_t const result =
			wait(syncobj, 200ll*1000*1000, SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT);

		if (result != -LX_ETIME)
			_fail("result ", result, ", expected ", -LX_ETIME);

		if (!_was_pending)
			_fail("the wait did not block");
	}

	_begin("SYNCOBJ_SIGNAL");
	{
		Message        msg     = _message(Op::SYNCOBJ_SIGNAL);
		Syncobj_array &array   = msg.payload<Syncobj_array>();
		uint32_t      &handles = *_need(msg.append<uint32_t>(1, 4));

		handles             = syncobj;
		array.handles       = msg.offset(&handles);
		array.count_handles = 1;

		_expect(msg, 0, "SYNCOBJ_SIGNAL");
	}

	_begin("SYNCOBJ_WAIT on the signaled syncobj, polling");
	{
		int32_t const result = wait(syncobj, 0, 0);
		if (result)
			_fail("result ", result);
	}

	_begin("SYNCOBJ_CREATE (for a timeline)");
	{
		Message         msg    = _message(Op::SYNCOBJ_CREATE);
		Syncobj_create &create = msg.payload<Syncobj_create>();

		_expect(msg, 0, "SYNCOBJ_CREATE");

		timeline = create.handle;
		log("  handle ", timeline);
	}

	_begin("VM_BIND, asynchronous: map at ", Hex((uint64_t)BO_VA_2),
	       ", signal point 1 of the timeline");
	{
		Message     msg  = _message(Op::VM_BIND);
		Vm_bind    &bind = msg.payload<Vm_bind>();
		Vm_bind_op &op   = *_need(msg.append<Vm_bind_op>(1));
		Sync_op    &sync = *_need(msg.append<Sync_op>(1));

		bind.vm_id = vm_id;
		bind.flags = VM_BIND_ASYNC;
		bind.ops   = msg.obj_array(&op, 1);

		op.flags     = VM_BIND_OP_TYPE_MAP | VM_BIND_OP_MAP_NOEXEC;
		op.bo_handle = bo_handle;
		op.va        = BO_VA_2;
		op.size      = BO_SIZE;
		op.syncs     = msg.obj_array(&sync, 1);

		sync.flags          = SYNC_OP_SIGNAL | SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ;
		sync.handle         = timeline;
		sync.timeline_value = 1;

		_expect(msg, 0, "VM_BIND async");
	}

	_begin("SYNCOBJ_TIMELINE_WAIT for point 1, time-out 2 s");
	{
		Message                msg     = _message(Op::SYNCOBJ_TIMELINE_WAIT);
		Syncobj_timeline_wait &w       = msg.payload<Syncobj_timeline_wait>();
		uint64_t              &points  = *_need(msg.append<uint64_t>(1));
		uint32_t              &handles = *_need(msg.append<uint32_t>(1, 4));

		points          = 1;
		handles         = timeline;
		w.handles       = msg.offset(&handles);
		w.points        = msg.offset(&points);
		w.count_handles = 1;
		w.timeout_nsec  = 2000ll*1000*1000;
		w.flags         = SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;

		int32_t const result = _execute(msg);

		log("  result ", result, ", was pending: ", _was_pending);

		if (result)
			_fail("the asynchronous VM_BIND did not complete: ", result);
	}

	_begin("SYNCOBJ_QUERY of the timeline");
	{
		Message                 msg     = _message(Op::SYNCOBJ_QUERY);
		Syncobj_timeline_array &array   = msg.payload<Syncobj_timeline_array>();
		uint64_t               &points  = *_need(msg.append<uint64_t>(1));
		uint32_t               &handles = *_need(msg.append<uint32_t>(1, 4));

		handles             = timeline;
		array.handles       = msg.offset(&handles);
		array.points        = msg.offset(&points);
		array.count_handles = 1;

		_expect(msg, 0, "SYNCOBJ_QUERY");

		log("  last signaled point ", points);
	}

	_begin("SYNCOBJ_TRANSFER: point 1 of the timeline to the binary syncobj");
	{
		Message           msg      = _message(Op::SYNCOBJ_TRANSFER);
		Syncobj_transfer &transfer = msg.payload<Syncobj_transfer>();

		transfer.src_handle = timeline;
		transfer.src_point  = 1;
		transfer.dst_handle = syncobj;

		_expect(msg, 0, "SYNCOBJ_TRANSFER");
	}

	int32_t sync_file = -1;

	_begin("SYNCOBJ_HANDLE_TO_FD: export the syncobj's fence as sync file");
	{
		Message         msg    = _message(Op::SYNCOBJ_HANDLE_TO_FD);
		Syncobj_handle &handle = msg.payload<Syncobj_handle>();

		handle.handle = syncobj;
		handle.flags  = SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE;
		handle.fd     = -1;

		_expect(msg, 0, "SYNCOBJ_HANDLE_TO_FD");

		sync_file = handle.fd;
		log("  descriptor ", sync_file, " (of the session's table in the driver)");
	}

	_begin("SYNCOBJ_FD_TO_HANDLE: import the sync file into the syncobj");
	{
		Message         msg    = _message(Op::SYNCOBJ_FD_TO_HANDLE);
		Syncobj_handle &handle = msg.payload<Syncobj_handle>();

		handle.handle = syncobj;
		handle.flags  = SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE;
		handle.fd     = sync_file;

		_expect(msg, 0, "SYNCOBJ_FD_TO_HANDLE");
	}

	_begin("FD_CLOSE, twice: the second one must end with EBADF");
	{
		Message   msg   = _message(Op::FD_CLOSE);
		Fd_close &close = msg.payload<Fd_close>();

		close.fd = sync_file;
		_expect(msg, 0, "FD_CLOSE");

		_header().state = Header::NEW;
		if (_execute_raw() != -LX_EBADF)
			_fail("second FD_CLOSE: result ", (int32_t)_header().result);
	}

	/*
	 * Tiler heap and group, with the values of Mesa's 'csf_init_context'
	 */

	uint32_t heap = 0, group = 0;

	_begin("TILER_HEAP_CREATE: 2 chunks of 2 MiB");
	{
		Message            msg    = _message(Op::TILER_HEAP_CREATE);
		Tiler_heap_create &create = msg.payload<Tiler_heap_create>();

		create.vm_id               = vm_id;
		create.initial_chunk_count = 2;
		create.chunk_size          = 2*1024*1024;
		create.max_chunks          = 64;
		create.target_in_flight    = 65535;

		_expect(msg, 0, "TILER_HEAP_CREATE");

		heap = create.handle;
		log("  handle ", Hex(heap), " context at ", Hex(create.tiler_heap_ctx_gpu_va),
		    " first chunk at ", Hex(create.first_heap_chunk_gpu_va));
	}

	_begin("GROUP_CREATE: one queue, ring buffer of 64 KiB");
	{
		auto bits = [] (uint64_t v) {
			uint8_t n = 0;
			for (; v; v >>= 1) n = (uint8_t)(n + (v & 1));
			return n; };

		Message       msg    = _message(Op::GROUP_CREATE);
		Group_create &create = msg.payload<Group_create>();
		Queue_create &queue  = *_need(msg.append<Queue_create>(1));

		queue.priority     = 1;
		queue.ringbuf_size = 64*1024;

		create.queues             = msg.obj_array(&queue, 1);
		create.compute_core_mask  = _info.gpu.shader_present;
		create.fragment_core_mask = _info.gpu.shader_present;
		create.tiler_core_mask    = 1;
		create.max_compute_cores  = bits(_info.gpu.shader_present);
		create.max_fragment_cores = bits(_info.gpu.shader_present);
		create.max_tiler_cores    = 1;
		create.priority           = GROUP_PRIORITY_MEDIUM;
		create.vm_id              = vm_id;

		_expect(msg, 0, "GROUP_CREATE");

		group = create.group_handle;
		log("  handle ", group);
	}

	auto group_state = [&] {

		Message          msg   = _message(Op::GROUP_GET_STATE);
		Group_get_state &state = msg.payload<Group_get_state>();

		state.group_handle = group;
		_expect(msg, 0, "GROUP_GET_STATE");

		log("  state ", Hex(state.state), " (1: timed out, 2: fatal fault, "
		    "4: innocent), fatal_queues ", Hex(state.fatal_queues));

		return state.state;
	};

	_begin("GROUP_GET_STATE");
	if (group_state())
		_fail("fresh group is not in state 0");

	/*
	 * Submission of a command stream of two NOP instructions (opcode 0 in
	 * bits 63..56 of an instruction). The kernel calls the stream from the
	 * queue's ring buffer; the firmware executes it and the kernel's
	 * trailing instructions, the job-done interrupt signals the syncobj.
	 */
	_begin("SYNCOBJ_RESET");
	{
		Message        msg     = _message(Op::SYNCOBJ_RESET);
		Syncobj_array &array   = msg.payload<Syncobj_array>();
		uint32_t      &handles = *_need(msg.append<uint32_t>(1, 4));

		handles             = syncobj;
		array.handles       = msg.offset(&handles);
		array.count_handles = 1;

		_expect(msg, 0, "SYNCOBJ_RESET");
	}

	_begin("GROUP_SUBMIT: stream of 2 NOP instructions at ", Hex((uint64_t)BO_VA));
	{
		uint64_t volatile * const stream = bo->local_addr<uint64_t volatile>();

		stream[0] = 0;
		stream[1] = 0;

		Message       msg    = _message(Op::GROUP_SUBMIT,
		                                Header::FLAG_SAMPLE_FLUSH_ID);
		Group_submit &submit = msg.payload<Group_submit>();
		Queue_submit &queue  = *_need(msg.append<Queue_submit>(1));
		Sync_op      &sync   = *_need(msg.append<Sync_op>(1));

		submit.group_handle  = group;
		submit.queue_submits = msg.obj_array(&queue, 1);

		queue.queue_index = 0;
		queue.stream_addr = BO_VA;
		queue.stream_size = 16;
		queue.syncs       = msg.obj_array(&sync, 1);

		sync.flags  = SYNC_OP_SIGNAL | SYNC_OP_HANDLE_TYPE_SYNCOBJ;
		sync.handle = syncobj;

		_expect(msg, 0, "GROUP_SUBMIT");

		log("  submitted with latest_flush ", queue.latest_flush);
	}

	_begin("SYNCOBJ_WAIT for the job, time-out 5 s");
	{
		int32_t const result =
			wait(syncobj, 5000ll*1000*1000, SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT);

		if (result)
			_fail("the job did not complete: ", result);
	}

	_begin("GROUP_GET_STATE after the job");
	if (group_state())
		_fail("the group is not in state 0 after the job");

	/*
	 * Destruction
	 */

	_begin("TILER_HEAP_DESTROY");
	{
		Message             msg     = _message(Op::TILER_HEAP_DESTROY);
		Tiler_heap_destroy &destroy = msg.payload<Tiler_heap_destroy>();

		destroy.handle = heap;
		_expect(msg, 0, "TILER_HEAP_DESTROY");
	}

	_begin("GROUP_DESTROY");
	{
		Message        msg     = _message(Op::GROUP_DESTROY);
		Group_destroy &destroy = msg.payload<Group_destroy>();

		destroy.group_handle = group;
		_expect(msg, 0, "GROUP_DESTROY");
	}

	_begin("VM_BIND, synchronous: unmap both mappings");
	{
		Message     msg  = _message(Op::VM_BIND);
		Vm_bind    &bind = msg.payload<Vm_bind>();
		Vm_bind_op *ops  = _need(msg.append<Vm_bind_op>(2));

		bind.vm_id = vm_id;
		bind.ops   = msg.obj_array(ops, 2);

		ops[0].flags = VM_BIND_OP_TYPE_UNMAP;
		ops[0].va    = BO_VA;
		ops[0].size  = BO_SIZE;

		ops[1].flags = VM_BIND_OP_TYPE_UNMAP;
		ops[1].va    = BO_VA_2;
		ops[1].size  = BO_SIZE;

		_expect(msg, 0, "VM_BIND unmap");
	}

	_begin("detach the buffer object and GEM_CLOSE");
	{
		bo.destruct();

		Message    msg   = _message(Op::GEM_CLOSE);
		Gem_close &close = msg.payload<Gem_close>();

		close.handle = bo_handle;
		_expect(msg, 0, "GEM_CLOSE");
	}

	_begin("SYNCOBJ_DESTROY of both syncobjs");
	uint32_t const syncobjs[] { syncobj, timeline };
	for (uint32_t const handle : syncobjs) {

		Message          msg     = _message(Op::SYNCOBJ_DESTROY);
		Syncobj_destroy &destroy = msg.payload<Syncobj_destroy>();

		destroy.handle = handle;
		_expect(msg, 0, "SYNCOBJ_DESTROY");
	}

	_cached_bo(vm_id);

	_begin("VM_DESTROY");
	{
		Message     msg     = _message(Op::VM_DESTROY);
		Vm_destroy &destroy = msg.payload<Vm_destroy>();

		destroy.id = vm_id;
		_expect(msg, 0, "VM_DESTROY");
	}

	_begin("NOP after the destruction");
	{
		Message msg = _message(Op::NOP);
		_expect(msg, 0, "NOP");
		log("  LATEST_FLUSH_ID ", _header().flush_id);
	}
}


/*
 * Cached buffer objects: BO_CREATE with BO_WB_MMAP, BO_QUERY_INFO, BO_SYNC
 * with valid and invalid ranges, cache maintenance by the client itself
 */
void Test::Main::_cached_bo(uint32_t vm_id)
{
	enum : uint64_t { SIZE = 1024*1024 };

	auto create = [&] (uint32_t flags, int32_t expected) {

		Message    msg = _message(Op::BO_CREATE);
		Bo_create &bo  = msg.payload<Bo_create>();

		bo.size            = SIZE;
		bo.flags           = flags;
		bo.exclusive_vm_id = vm_id;

		_expect(msg, expected, "BO_CREATE");

		if (bo.flags != flags)
			_fail("BO_CREATE returned the flags changed: ", Hex(bo.flags));

		return expected ? 0u : (uint32_t)bo.handle;
	};

	auto close = [&] (uint32_t handle) {

		Message msg = _message(Op::GEM_CLOSE);
		msg.payload<Gem_close>().handle = handle;
		_expect(msg, 0, "GEM_CLOSE");
	};

	if (!_info.bo_cached) {
		_begin("BO_CREATE with BO_WB_MMAP is refused (driver without "
		       "cached buffer objects)");
		(void)create(BO_WB_MMAP, -LX_EINVAL);

		Message msg = _message(Op::BO_QUERY_INFO);
		msg.payload<Bo_query_info>().handle = 1;
		_expect(msg, -LX_ENOSYS, "BO_QUERY_INFO");
		return;
	}

	_begin("BO_CREATE with BO_WB_MMAP | BO_NO_MMAP is refused");
	(void)create(BO_WB_MMAP | BO_NO_MMAP, -LX_EINVAL);

	_begin("BO_CREATE of a cached (BO_WB_MMAP) and an uncached object, ",
	       Hex((uint64_t)SIZE), " bytes each");

	uint32_t const cached   = create(BO_WB_MMAP, 0);
	uint32_t const uncached = create(0, 0);

	log("  handles ", cached, " (cached), ", uncached, " (uncached)");

	_begin("BO_QUERY_INFO");
	{
		auto query = [&] (uint32_t handle, int32_t expected, uint32_t flags) {

			Message        msg  = _message(Op::BO_QUERY_INFO);
			Bo_query_info &info = msg.payload<Bo_query_info>();

			info.handle = handle;
			_expect(msg, expected, "BO_QUERY_INFO");

			if (!expected && (info.create_flags != flags || info.extra_flags))
				_fail("object ", handle, ": create_flags ", Hex(info.create_flags),
				      ", extra_flags ", Hex(info.extra_flags));
		};

		query(cached,     0,          BO_WB_MMAP);
		query(uncached,   0,          0);
		query(0x7fffffff, -LX_ENOENT, 0);

		Message msg = _message(Op::BO_QUERY_INFO);
		Bo_query_info &with_pad = msg.payload<Bo_query_info>();
		with_pad.handle = cached;
		with_pad.pad    = 1;
		_expect(msg, -LX_EINVAL, "BO_QUERY_INFO with pad");
	}

	auto attach = [&] (Constructible<Attached_dataspace> &ds, uint32_t handle) {

		Dataspace_capability const cap =
			_gpu.map_cpu(Gpu::Vram_id { .value = handle },
			             Gpu::Mapping_attributes::rw());
		if (!cap.valid())
			_fail("map_cpu of object ", handle, " returned no dataspace");

		ds.construct(_env.rm(), cap);

		if (ds->size() < SIZE)
			_fail("dataspace of object ", handle, " smaller than the object");
	};

	Constructible<Attached_dataspace> cached_ds { }, uncached_ds { };

	_begin("map_cpu of both objects, a fresh cached object reads zero");
	{
		attach(cached_ds,   cached);
		attach(uncached_ds, uncached);

		uint64_t volatile const * const words =
			cached_ds->local_addr<uint64_t volatile const>();

		for (unsigned i = 0; i < SIZE/sizeof(uint64_t); i++)
			if (words[i])
				_fail("word ", i, " of the fresh object reads ", Hex(words[i]));
	}

	auto pattern = [] (unsigned i) { return 0x5a5a0000u ^ (i*0x10001u); };

	unsigned const count = SIZE/sizeof(uint32_t);

	auto fill = [&] (Attached_dataspace &ds) {
		uint32_t volatile * const words = ds.local_addr<uint32_t volatile>();
		for (unsigned i = 0; i < count; i++)
			words[i] = pattern(i);
	};

	auto verify = [&] (Attached_dataspace &ds, char const *what) {
		uint32_t volatile const * const words =
			ds.local_addr<uint32_t volatile const>();
		for (unsigned i = 0; i < count; i++)
			if (words[i] != pattern(i))
				_fail(what, ": word ", i, " reads ", Hex(words[i]),
				      ", written ", Hex(pattern(i)));
	};

	_begin("write and read both objects (time of 8 passes over 1 MiB)");
	{
		auto measure = [&] (Attached_dataspace &ds, char const *name) {

			uint64_t const t0 = _timer.elapsed_us();
			for (unsigned pass = 0; pass < 8; pass++)
				fill(ds);

			uint64_t const t1 = _timer.elapsed_us();
			for (unsigned pass = 0; pass < 8; pass++)
				verify(ds, name);

			uint64_t const t2 = _timer.elapsed_us();

			log("  ", name, ": write ", (t1 - t0)/8, " us, read and compare ",
			    (t2 - t1)/8, " us per pass");
		};

		measure(*cached_ds,   "cached");
		measure(*uncached_ds, "uncached");
	}

	auto sync = [&] (uint32_t handle, uint32_t type, uint64_t offset,
	                 uint64_t size, int32_t expected, char const *what) {

		Message     msg = _message(Op::BO_SYNC);
		Bo_sync    &s   = msg.payload<Bo_sync>();
		Bo_sync_op &op  = *_need(msg.append<Bo_sync_op>(1));

		op  = { .handle = handle, .type = type, .offset = offset, .size = size };
		s.ops = msg.obj_array(&op, 1);

		_expect(msg, expected, what);
	};

	_begin("BO_SYNC of valid ranges, the content stays");
	{
		uint64_t const t0 = _timer.elapsed_us();
		sync(cached, BO_SYNC_CPU_CACHE_FLUSH, 0, SIZE, 0, "flush of the whole object");
		uint64_t const t1 = _timer.elapsed_us();

		sync(cached, BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE, 0, SIZE, 0,
		     "flush and invalidate of the whole object");
		sync(cached, BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE, 0x1234, 0x101, 0,
		     "unaligned range");
		sync(cached, BO_SYNC_CPU_CACHE_FLUSH, SIZE - 1, 1, 0, "last byte");
		sync(cached, BO_SYNC_CPU_CACHE_FLUSH, SIZE, 0, 0, "empty range at the end");
		sync(uncached, BO_SYNC_CPU_CACHE_FLUSH, 0, SIZE, 0, "uncached object");

		verify(*cached_ds,   "cached object after BO_SYNC");
		verify(*uncached_ds, "uncached object after BO_SYNC");

		log("  BO_SYNC of 1 MiB took ", t1 - t0, " us");
	}

	_begin("BO_SYNC of invalid ranges is refused");
	{
		sync(cached, BO_SYNC_CPU_CACHE_FLUSH, 0, SIZE + 1, -LX_EINVAL, "one byte too long");
		sync(cached, BO_SYNC_CPU_CACHE_FLUSH, SIZE, 1, -LX_EINVAL, "behind the end");
		sync(cached, BO_SYNC_CPU_CACHE_FLUSH, SIZE + 1, 0, -LX_EINVAL,
		     "empty range behind the end");
		sync(cached, BO_SYNC_CPU_CACHE_FLUSH, ~0ull, 2, -LX_EINVAL, "wrapping range");
		sync(cached, BO_SYNC_CPU_CACHE_FLUSH, 0x1000, ~0ull - 0xfff, -LX_EINVAL,
		     "range that wraps to 0");
		sync(uncached, BO_SYNC_CPU_CACHE_FLUSH, 0, SIZE + 1, -LX_EINVAL,
		     "uncached object, too long");
		sync(cached, 2, 0, SIZE, -LX_EINVAL, "unknown type");
		sync(0x7fffffff, BO_SYNC_CPU_CACHE_FLUSH, 0, 1, -LX_ENOENT, "unknown handle");

		/* more than MAX_BO_SYNC_BYTES in one request, each range valid */
		unsigned const ops_count = (unsigned)((uint64_t)MAX_BO_SYNC_BYTES/(uint64_t)SIZE) + 1;

		Message     msg = _message(Op::BO_SYNC);
		Bo_sync    &s   = msg.payload<Bo_sync>();
		Bo_sync_op *ops = _need(msg.append<Bo_sync_op>(ops_count));

		for (unsigned i = 0; i < ops_count; i++)
			ops[i] = { .handle = cached, .type = BO_SYNC_CPU_CACHE_FLUSH,
			           .offset = 0, .size = SIZE };

		s.ops = msg.obj_array(ops, ops_count);
		_expect(msg, -LX_EINVAL, "request above the byte limit");

		/* a refused request has no effect, a valid one still works */
		sync(cached, BO_SYNC_CPU_CACHE_FLUSH, 0, SIZE, 0, "valid range afterwards");
		verify(*cached_ds, "cached object after the refused requests");
	}

	_begin("cache maintenance by the client (clean and invalidate of its mapping)");
	{
		/*
		 * What Mesa does for its cached mappings: it reads the line size
		 * from CTR_EL0 and issues 'dc cvac' / 'dc civac' in user mode.
		 * Both need the kernel's consent (SCTLR_EL1.UCT / UCI); without
		 * it this step ends with an exception of the test.
		 */
		uint64_t ctr = 0;
		asm volatile ("mrs %0, ctr_el0" : "=r" (ctr));
		log("  CTR_EL0 ", Hex(ctr), ": data cache line ",
		    4u << ((ctr >> 16) & 0xf), " bytes, write-back granule ",
		    4u << ((ctr >> 24) & 0xf), " bytes");

		fill(*cached_ds);

		for (unsigned i = 0; i < SIZE; i += 64)
			asm volatile ("dc cvac, %0" : : "r" (cached_ds->local_addr<char>() + i)
			                            : "memory");
		asm volatile ("dsb sy");

		verify(*cached_ds, "cached object after the client's 'dc cvac'");

		uint64_t const t0 = _timer.elapsed_us();
		cache_clean_invalidate_data((addr_t)cached_ds->local_addr<void>(), SIZE);
		uint64_t const t1 = _timer.elapsed_us();

		verify(*cached_ds, "cached object after the client's cache operation");

		log("  1 MiB took ", t1 - t0, " us");
	}

	_begin("detach and GEM_CLOSE of both objects");
	{
		cached_ds.destruct();
		uncached_ds.destruct();
		close(cached);
		close(uncached);
	}
}


void Component::construct(Genode::Env &env)
{
	static Test::Main main(env);
}
