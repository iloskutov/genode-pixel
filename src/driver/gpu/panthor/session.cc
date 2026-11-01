/*
 * \brief  Gpu session of the panthor GPU driver
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Structure of the lima port (repos/allwinner/src/driver/gpu/lima): one
 * pseudo DRM file and one Linux task per session; the entrypoint hands a
 * request to the task and runs the scheduler; a request whose task blocked
 * is reported as pending and completed with the session's completion
 * signal.
 *
 * Unlike lima, the requests are not bent onto the session's RPC functions
 * one by one: a client composes the argument structure of the DRM ioctl in
 * an exchange buffer and passes it through 'execute'. The protocol is
 * described in include/gpu/panthor_session.h.
 *
 *   alloc_vram / free_vram   exchange buffers (plain RAM)
 *   execute / complete       one message = one ioctl
 *   map_cpu                  dataspace of a buffer object (GEM handle)
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/attached_ram_dataspace.h>
#include <base/heap.h>
#include <base/id_space.h>
#include <base/log.h>
#include <base/session_object.h>
#include <base/signal.h>
#include <gpu/panthor_session.h>
#include <gpu_session/gpu_session.h>
#include <root/component.h>
#include <util/reconstructible.h>

/* emulation includes */
#include <lx_emul/task.h>
#include <lx_kit/env.h>

/* local includes */
#include "emul.h"
#include "lx_drm.h"
#include "session.h"
#include "shared_buffer.h"
#include "tunnel.h"

namespace Gpu {

	using namespace Genode;

	struct Worker;
	class  Session_component;
	class  Root;

	using Root_component = Genode::Root_component<Session_component,
	                                              Genode::Multiple_clients>;
}


/*
 * The request numbers of the C side are the ones of the protocol
 */
#define ASSERT_SAME_OP(name) \
	static_assert((unsigned)Gpu::Panthor::Op::name == (unsigned)LX_DRM_##name, \
	              "request number of " #name " differs");

ASSERT_SAME_OP(NOP)                   ASSERT_SAME_OP(DEV_QUERY)
ASSERT_SAME_OP(VM_CREATE)             ASSERT_SAME_OP(VM_DESTROY)
ASSERT_SAME_OP(VM_BIND)               ASSERT_SAME_OP(VM_GET_STATE)
ASSERT_SAME_OP(BO_CREATE)             ASSERT_SAME_OP(GROUP_CREATE)
ASSERT_SAME_OP(GROUP_DESTROY)         ASSERT_SAME_OP(GROUP_SUBMIT)
ASSERT_SAME_OP(GROUP_GET_STATE)       ASSERT_SAME_OP(TILER_HEAP_CREATE)
ASSERT_SAME_OP(TILER_HEAP_DESTROY)    ASSERT_SAME_OP(BO_SET_LABEL)
ASSERT_SAME_OP(GEM_CLOSE)             ASSERT_SAME_OP(SYNCOBJ_CREATE)
ASSERT_SAME_OP(SYNCOBJ_DESTROY)       ASSERT_SAME_OP(SYNCOBJ_HANDLE_TO_FD)
ASSERT_SAME_OP(SYNCOBJ_FD_TO_HANDLE)  ASSERT_SAME_OP(SYNCOBJ_WAIT)
ASSERT_SAME_OP(SYNCOBJ_RESET)         ASSERT_SAME_OP(SYNCOBJ_SIGNAL)
ASSERT_SAME_OP(SYNCOBJ_TIMELINE_WAIT) ASSERT_SAME_OP(SYNCOBJ_QUERY)
ASSERT_SAME_OP(SYNCOBJ_TRANSFER)      ASSERT_SAME_OP(SYNCOBJ_TIMELINE_SIGNAL)
ASSERT_SAME_OP(FD_CLOSE)              ASSERT_SAME_OP(BO_SYNC)
ASSERT_SAME_OP(TEST_SLEEP)            ASSERT_SAME_OP(BO_QUERY_INFO)

static_assert((unsigned)Gpu::Panthor::Header::FLAG_SAMPLE_FLUSH_ID
           == (unsigned)LX_DRM_FLAG_SAMPLE_FLUSH_ID);

static Gpu::Panthor::Op const _ops[] = {
	Gpu::Panthor::Op::NOP,                   Gpu::Panthor::Op::DEV_QUERY,
	Gpu::Panthor::Op::VM_CREATE,             Gpu::Panthor::Op::VM_DESTROY,
	Gpu::Panthor::Op::VM_BIND,               Gpu::Panthor::Op::VM_GET_STATE,
	Gpu::Panthor::Op::BO_CREATE,             Gpu::Panthor::Op::GROUP_CREATE,
	Gpu::Panthor::Op::GROUP_DESTROY,         Gpu::Panthor::Op::GROUP_SUBMIT,
	Gpu::Panthor::Op::GROUP_GET_STATE,       Gpu::Panthor::Op::TILER_HEAP_CREATE,
	Gpu::Panthor::Op::TILER_HEAP_DESTROY,    Gpu::Panthor::Op::BO_SET_LABEL,
	Gpu::Panthor::Op::GEM_CLOSE,             Gpu::Panthor::Op::SYNCOBJ_CREATE,
	Gpu::Panthor::Op::SYNCOBJ_DESTROY,       Gpu::Panthor::Op::SYNCOBJ_HANDLE_TO_FD,
	Gpu::Panthor::Op::SYNCOBJ_FD_TO_HANDLE,  Gpu::Panthor::Op::SYNCOBJ_WAIT,
	Gpu::Panthor::Op::SYNCOBJ_RESET,         Gpu::Panthor::Op::SYNCOBJ_SIGNAL,
	Gpu::Panthor::Op::SYNCOBJ_TIMELINE_WAIT, Gpu::Panthor::Op::SYNCOBJ_QUERY,
	Gpu::Panthor::Op::SYNCOBJ_TRANSFER,      Gpu::Panthor::Op::SYNCOBJ_TIMELINE_SIGNAL,
	Gpu::Panthor::Op::FD_CLOSE,              Gpu::Panthor::Op::BO_SYNC,
	Gpu::Panthor::Op::TEST_SLEEP,            Gpu::Panthor::Op::BO_QUERY_INFO,
};


/* implemented in 'lx_user.c' */
extern "C" struct task_struct *lx_user_task;
extern "C" void               *lx_user_task_args;
extern "C" struct task_struct *lx_user_new_gpu_task(int (*func)(void *), void *args);
extern "C" void                lx_user_destroy_gpu_task(struct task_struct *);


/**
 * State shared by a session and its Linux task
 *
 * Allocated from the driver's heap and destroyed by the task when it
 * exits: a task that is blocked inside the kernel when its session is
 * closed finishes later and must find its request and its message intact.
 */
struct Gpu::Worker
{
	enum class Local { NONE, OPEN, BO_DATASPACE };

	struct Notifier : Interface { virtual void notify() = 0; };

	/* cleared when the session is destroyed */
	Notifier *notifier { nullptr };

	/* the session is gone: close the DRM file and exit as soon as possible */
	bool orphan { false };

	struct task_struct *task { nullptr };
	void               *drm  { nullptr };

	/*
	 * Request of the session itself
	 */
	Local                local      { Local::NONE };
	bool                 local_done { false };
	uint32_t             bo_handle  { 0 };
	Dataspace_capability bo_cap     { };
	Panthor::Info        info       { };

	/*
	 * Message of the client: the task executes it while 'busy && !done'
	 */
	bool     busy     { false };
	bool     done     { false };
	bool     canceled { false };
	int      result   { 0 };
	uint32_t flush_id { 0 };

	/* private, relocated copy of the message */
	alignas(16) char msg[Panthor::MAX_MESSAGE_SIZE];

	void open()
	{
		using namespace Panthor;

		drm = lx_drm_open();

		info = { };
		info.magic                    = MAGIC;
		info.version                  = VERSION;
		info.bo_cached                = 0;
		info.max_message_size         = MAX_MESSAGE_SIZE;
		info.max_exchange_buffer_size = MAX_EXCHANGE_BUFFER_SIZE;
		info.max_exchange_buffers     = MAX_EXCHANGE_BUFFERS;

		if (!drm)
			return;

		/* DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE */
		info.drm_core             = 1;
		info.cap_syncobj          = 1;
		info.cap_syncobj_timeline = 1;

		int const err = lx_drm_info(drm, &info.drm_version_major,
		                            &info.drm_version_minor,
		                            &info.drm_version_patch,
		                            &info.gpu,  sizeof(info.gpu),
		                            &info.csif, sizeof(info.csif));
		if (err) {
			error("device query for the session info failed: ", err);
			lx_drm_close(drm);
			drm  = nullptr;
			info.drm_core = info.cap_syncobj = info.cap_syncobj_timeline = 0;
			return;
		}

		info.device = 1;

		/* cached buffer objects on request (WB_MMAP), BO_SYNC, BO_QUERY_INFO */
		info.bo_cached = emul_config_bo_cached();
	}

	void lookup_bo()
	{
		unsigned long size = 0;

		struct genode_shared_dataspace * const ds =
			lx_drm_bo_dataspace(drm, bo_handle, &size);

		bo_cap = ds ? genode_shared_dataspace_capability(ds)
		            : Dataspace_capability();
	}

	void execute_message()
	{
		Panthor::Header const &header = *(Panthor::Header const *)msg;

		char * const payload = msg + sizeof(Panthor::Header);

		result = lx_drm_tunnel(drm, header.op, header.flags, payload);

		if (canceled) {
			lx_drm_cancel_done();
			canceled = false;
		}

		flush_id = lx_drm_flush_id(drm);
		done     = true;

		if (notifier)
			notifier->notify();
	}
};


/**
 * Function executed by the task of a session
 */
extern "C" int gpu_task_func(void *p)
{
	Gpu::Worker &worker = *static_cast<Gpu::Worker *>(p);

	for (;;) {

		switch (worker.local) {
		case Gpu::Worker::Local::OPEN:         worker.open();      break;
		case Gpu::Worker::Local::BO_DATASPACE: worker.lookup_bo(); break;
		case Gpu::Worker::Local::NONE:                             break;
		}

		if (worker.local != Gpu::Worker::Local::NONE) {
			worker.local      = Gpu::Worker::Local::NONE;
			worker.local_done = true;
		}

		if (worker.busy && !worker.done)
			worker.execute_message();

		if (worker.orphan)
			break;

		lx_emul_task_schedule(true);
	}

	lx_drm_close(worker.drm);

	struct task_struct * const task = worker.task;

	Genode::destroy(Lx_kit::env().heap, &worker);

	lx_user_destroy_gpu_task(task);
	return 0;
}


/*
 * The session tasks are created by the 'lx_user' task, i.e., from within
 * the kernel context (as in the lima port)
 */
struct Lx_user_task_args
{
	bool  create_task;
	void *args;

	struct task_struct *new_gpu_task;
};

static Lx_user_task_args _lx_user_task_args { };


extern "C" int lx_user_task_func(void *p)
{
	Lx_user_task_args &args = *static_cast<Lx_user_task_args *>(p);

	while (true) {
		if (args.create_task) {
			args.new_gpu_task = lx_user_new_gpu_task(gpu_task_func, args.args);
			args.create_task  = false;
		}

		lx_emul_task_schedule(true);
	}
}


static struct task_struct *create_gpu_task(void *args)
{
	if (_lx_user_task_args.create_task || !lx_user_task)
		return nullptr;

	_lx_user_task_args.args         = args;
	_lx_user_task_args.new_gpu_task = nullptr;
	_lx_user_task_args.create_task  = true;

	lx_emul_task_unblock(lx_user_task);
	Lx_kit::env().scheduler.execute();

	return _lx_user_task_args.new_gpu_task;
}


class Gpu::Session_component : public Genode::Session_object<Gpu::Session>
{
	private:

		Session_component(Session_component const &) = delete;
		Session_component &operator = (Session_component const &) = delete;

		struct Exchange_buffer;
		using Buffer_space = Id_space<Exchange_buffer>;

		struct Exchange_buffer
		{
			Attached_ram_dataspace ds;

			Buffer_space::Element const elem;

			Exchange_buffer(Env &env, Buffer_space &space, Vram_id id, size_t size)
			:
				ds(env.ram(), env.rm(), size),
				elem(*this, space, Buffer_space::Id { .value = id.value })
			{ }
		};

		Env &_env;

		Heap _heap { _env.ram(), _env.rm() };

		Buffer_space _buffers      { };
		unsigned     _buffer_count { 0 };

		Attached_ram_dataspace _info_ds { _env.ram(), _env.rm(), 4096 };

		Signal_context_capability _completion_sigh { };

		struct Notifier : Worker::Notifier
		{
			Session_component &_session;

			Signal_handler<Notifier> _handler;

			void _handle() { _session._handle_worker(); }

			Notifier(Entrypoint &ep, Session_component &session)
			:
				_session(session), _handler(ep, *this, &Notifier::_handle)
			{ }

			void notify() override { _handler.local_submit(); }
		};

		Notifier _notifier { _env.ep(), *this };

		/* owned by its task */
		Worker &_worker { *new (Lx_kit::env().heap) Worker() };

		/*
		 * The message in flight
		 */
		struct Pending
		{
			Vram_id  id;
			size_t   offset;
			uint32_t size;
			uint64_t seqno;
		};

		Pending  _pending     { };
		uint64_t _seqno       { 0 };
		bool     _signal_owed { false };

		void _run_worker()
		{
			lx_emul_task_unblock(_worker.task);
			Lx_kit::env().scheduler.execute();
		}

		/**
		 * Let the task do something for the session itself
		 *
		 * \return false if the task is occupied or blocked on the way
		 */
		bool _local(Worker::Local local)
		{
			if (_worker.busy)
				return false;

			_worker.local      = local;
			_worker.local_done = false;

			_run_worker();

			return _worker.local_done;
		}

		void _with_buffer(Vram_id id, auto const &fn)
		{
			try {
				_buffers.apply<Exchange_buffer>(Buffer_space::Id { .value = id.value }, fn);
			} catch (Buffer_space::Unknown_id) { }
		}

		/**
		 * Hand the response of a completed message to the client
		 *
		 * \return true if a message was completed
		 */
		bool _finish()
		{
			using namespace Panthor;

			if (!_worker.busy || !_worker.done)
				return false;

			char   * const msg    = _worker.msg;
			Header &header        = *(Header *)msg;
			uint32_t const size   = _pending.size;

			Tunnel::unrelocate(msg, (uint64_t)msg);

			header.size     = size;
			header.result   = _worker.result;
			header.flush_id = _worker.flush_id;
			header.state    = Header::DONE;

			/* the client may have freed the buffer meanwhile */
			_with_buffer(_pending.id, [&] (Exchange_buffer &buffer) {

				if (_pending.offset > buffer.ds.size()
				 || size > buffer.ds.size() - _pending.offset)
					return;

				char * const shared = buffer.ds.local_addr<char>() + _pending.offset;

				/* the state last: the client may poll it */
				memcpy(shared + sizeof(Header), msg + sizeof(Header),
				       size - sizeof(Header));

				Header &shared_header = *(Header *)shared;

				shared_header.size     = header.size;
				shared_header.result   = header.result;
				shared_header.flush_id = header.flush_id;
				shared_header.state    = header.state;
			});

			_worker.busy = false;
			_worker.done = false;
			return true;
		}

		void _handle_worker()
		{
			(void)_finish();

			if (!_signal_owed || _worker.busy)
				return;

			_signal_owed = false;

			if (_completion_sigh.valid())
				Signal_transmitter(_completion_sigh).submit();
		}

		void _unused(char const *name)
		{
			warning(name, " is not used by the panthor driver's Gpu session");
		}

	public:

		/**
		 * Constructor
		 *
		 * \throw Service_denied
		 */
		Session_component(Env             &env,
		                  Resources const &resources,
		                  Label     const &label)
		:
			Session_object(env.ep(), resources, label), _env(env)
		{
			_worker.notifier = &_notifier;
			_worker.task     = create_gpu_task(&_worker);

			if (!_worker.task) {
				error("could not create the task of a Gpu session");
				destroy(Lx_kit::env().heap, &_worker);
				throw Service_denied();
			}

			bool const opened = _local(Worker::Local::OPEN);

			if (!opened || !_worker.drm) {
				error("Gpu session '", label, "' denied: ",
				      opened ? "no DRM device" : "opening the DRM device blocked");

				/* the task closes what it opened and frees the worker */
				_worker.notifier = nullptr;
				_worker.orphan   = true;
				_run_worker();
				throw Service_denied();
			}

			*_info_ds.local_addr<Panthor::Info>() = _worker.info;

		}

		~Session_component()
		{
			_worker.notifier = nullptr;

			/* interrupt a wait of the task */
			if (_worker.busy && !_worker.done) {
				_worker.canceled = true;
				lx_drm_cancel(_worker.task);
				Lx_kit::env().scheduler.execute();
			}

			if (_worker.busy && !_worker.done)
				warning("Gpu session '", label(), "' closed while its task is "
				        "blocked, the DRM file is closed when the task returns");

			/*
			 * The task closes the DRM file (groups, VMs, buffer objects,
			 * syncobjs, file descriptors) and destroys the worker, now or,
			 * if it is blocked, once it gets there.
			 */
			_worker.orphan = true;
			_run_worker();

			while (_buffers.apply_any<Exchange_buffer>([&] (Exchange_buffer &buffer) {
				destroy(_heap, &buffer); })) { }
		}


		/***************************
		 ** Gpu session interface **
		 ***************************/

		Dataspace_capability info_dataspace() const override
		{
			return _info_ds.cap();
		}

		Sequence_number execute(Vram_id id, off_t offset) override
		{
			using namespace Panthor;

			(void)_finish();

			if (_worker.busy)
				throw Invalid_state();

			char  *shared = nullptr;
			size_t avail  = 0;

			_with_buffer(id, [&] (Exchange_buffer &buffer) {

				size_t const size = buffer.ds.size();

				if (offset < 0 || (offset & 7) || (size_t)offset > size
				 || size - (size_t)offset < sizeof(Header))
					return;

				shared = buffer.ds.local_addr<char>() + offset;
				avail  = min(size - (size_t)offset, (size_t)MAX_MESSAGE_SIZE);
			});

			if (!shared)
				throw Invalid_state();

			Sequence_number const seqno { .value = ++_seqno };

			auto respond = [&] (int32_t result) {
				Header &header  = *(Header *)shared;
				header.result   = result;
				header.flush_id = 0;
				header.state    = Header::DONE;
				return seqno;
			};

			/* read once, the buffer is shared with the client */
			uint32_t const size = ((Header const volatile *)shared)->size;

			if (size < sizeof(Header) || size > avail)
				return respond(-LX_EPROTO);

			char * const msg = _worker.msg;

			memcpy(msg, shared, size);
			((Header *)msg)->size = size;

			switch (Tunnel::check(msg, size)) {
			case Tunnel::Check::OK:         break;
			case Tunnel::Check::UNKNOWN_OP: return respond(-LX_ENOSYS);
			default:                        return respond(-LX_EPROTO);
			}

			Tunnel::relocate(msg, (uint64_t)msg);

			_pending = { .id     = id,
			             .offset = (size_t)offset,
			             .size   = size,
			             .seqno  = seqno.value };

			_worker.result = 0;
			_worker.done   = false;
			_worker.busy   = true;

			_run_worker();

			if (!_finish()) {
				((Header *)shared)->state = Header::PENDING;
				_signal_owed = true;
			}

			return seqno;
		}

		bool complete(Sequence_number seqno) override
		{
			(void)_finish();

			return !(_worker.busy && _pending.seqno == seqno.value);
		}

		void completion_sigh(Signal_context_capability sigh) override
		{
			_completion_sigh = sigh;
		}

		Dataspace_capability alloc_vram(Vram_id id, size_t size) override
		{
			if (!size || size > Panthor::MAX_EXCHANGE_BUFFER_SIZE
			 || _buffer_count >= Panthor::MAX_EXCHANGE_BUFFERS) {
				warning("exchange buffer of ", size, " bytes refused (",
				        _buffer_count, " buffers)");
				return Dataspace_capability();
			}

			try {
				Exchange_buffer &buffer =
					*new (_heap) Exchange_buffer(_env, _buffers, id, size);

				_buffer_count++;
				return buffer.ds.cap();

			} catch (...) {
				warning("exchange buffer ", id.value, " could not be created");
				return Dataspace_capability();
			}
		}

		void free_vram(Vram_id id) override
		{
			Exchange_buffer *buffer_ptr = nullptr;

			_with_buffer(id, [&] (Exchange_buffer &buffer) {
				buffer_ptr = &buffer; });

			if (!buffer_ptr)
				return;

			destroy(_heap, buffer_ptr);
			_buffer_count--;
		}

		Dataspace_capability map_cpu(Vram_id id, Mapping_attributes) override
		{
			(void)_finish();

			_worker.bo_handle = (uint32_t)id.value;
			_worker.bo_cap    = Dataspace_capability();

			if (!_worker.drm || !_local(Worker::Local::BO_DATASPACE))
				return Dataspace_capability();

			return _worker.bo_cap;
		}

		void unmap_cpu(Vram_id) override { }

		bool map_gpu(Vram_id, size_t, off_t, Virtual_address) override
		{
			_unused("map_gpu");
			return false;
		}

		void unmap_gpu(Vram_id, off_t, Virtual_address) override
		{
			_unused("unmap_gpu");
		}

		bool set_tiling_gpu(Vram_id, off_t, unsigned) override
		{
			_unused("set_tiling_gpu");
			return false;
		}

		Vram_capability export_vram(Vram_id) override
		{
			_unused("export_vram");
			return Vram_capability();
		}

		void import_vram(Vram_capability, Vram_id) override { _unused("import_vram"); }
};


class Gpu::Root : public Gpu::Root_component
{
	private:

		Root(Root const &) = delete;
		Root &operator = (Root const &) = delete;

		Env &_env;

	protected:

		Create_result _create_session(char const *args) override
		{
			return *new (md_alloc())
				Session_component(_env,
				                  session_resources_from_args(args),
				                  session_label_from_args(args));
		}

		void _upgrade_session(Session_component &session, char const *args) override
		{
			session.upgrade(ram_quota_from_args(args));
			session.upgrade(cap_quota_from_args(args));
		}

		void _destroy_session(Session_component &session) override
		{
			Genode::destroy(md_alloc(), &session);
		}

	public:

		Root(Env &env, Allocator &alloc)
		:
			Root_component(env.ep(), alloc), _env(env)
		{ }
};


void Gpu::prepare()
{
	_lx_user_task_args = { };
	lx_user_task_args  = &_lx_user_task_args;
}


/**
 * Called by 'lx_user_init' (a kernel task) once the initcalls, and with
 * them panthor's probe function, are done
 */
extern "C" void lx_emul_announce_gpu_session(void)
{
	using namespace Genode;

	static Constructible<Gpu::Root> root { };

	if (root.constructed())
		return;

	/* the kernel's argument structures must be the ones of the protocol */
	for (Gpu::Panthor::Op const op : _ops) {
		unsigned const kernel   = lx_drm_payload_size((unsigned)op);
		unsigned const protocol = Gpu::Panthor::Tunnel::payload_size(op);

		if (kernel != protocol) {
			error("Gpu service not announced: argument size of request ",
			      Hex((unsigned)op), " is ", kernel, " in the kernel, ",
			      protocol, " in the protocol");
			return;
		}
	}

	void * const drm     = lx_drm_open();
	bool   const present = (drm != nullptr);

	lx_drm_close(drm);

	if (!present) {
		error("Gpu service not announced: no DRM device, the probe of the "
		      "GPU driver failed");
		return;
	}

	root.construct(Lx_kit::env().env, Lx_kit::env().heap);

	Lx_kit::env().env.parent().announce(
		Lx_kit::env().env.ep().manage(*root));

	log("Gpu service announced (protocol ",
	    (unsigned)Gpu::Panthor::VERSION_MAJOR, ".",
	    (unsigned)Gpu::Panthor::VERSION_MINOR,
	    present ? ")" : ", without device)");
}
