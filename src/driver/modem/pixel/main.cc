/*
 * \brief  DDE modem driver of the Pixel 7
 * \author Ivan Loskutov
 * \date   2026-11-07
 *
 * Provides the device nodes of the cpif as Android_chardev sessions
 * (lx_user.c). The clients drive the modem through them as the vendor
 * daemons do on Linux.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/mutex.h>
#include <base/rpc_client.h>
#include <base/semaphore.h>
#include <base/thread.h>
#include <pd_session/pd_session.h>
#include <util/reconstructible.h>

#include <android_chardev_server/server.h>

#include <lx_emul/init.h>
#include <lx_kit/env.h>
#include <lx_kit/init.h>
#include <lx_user/io.h>

#include <lx_bridge.h>

namespace Pixel_modem {
	using namespace Genode;

	struct System_control;
	struct Request;
	struct Backend;
	struct Notifier;
	struct Main;

	using Android_chardev_server::Node;
	using Chardev = Android_chardev_session::Session;
}


/**
 * Client of core's system-control interface, which issues an SMC with the
 * registers x0..x7 of the state and returns x0..x3. Core hands out the
 * capability to components started with 'managing_system: yes'.
 */
struct Pixel_modem::System_control : Rpc_client<Pd_session::System_control>
{
	using State = Pd_session::Managing_system_state;

	explicit System_control(Capability<Pd_session::System_control> cap)
	: Rpc_client<Pd_session::System_control>(cap) { }

	State system_control(State const &state) override {
		return call<Rpc_system_control>(state); }
};


static Genode::Constructible<Pixel_modem::System_control> _system_control;


extern "C" void pixel_modem_smc(unsigned long const a[8], unsigned long r[4])
{
	Pixel_modem::System_control::State in { };
	for (unsigned i = 0; i < 8; i++)
		in.r[i] = a[i];

	Pixel_modem::System_control::State const out = _system_control->system_control(in);

	for (unsigned i = 0; i < 4; i++)
		r[i] = out.r[i];
}


struct Pixel_modem::Request : lx_bridge_request
{
	Blockade done { };

	Request(lx_bridge_op op, char const *name, void *node, void *buf,
	        unsigned long len, unsigned long out_cap,
	        unsigned long cmd, unsigned long arg)
	:
		lx_bridge_request { op, name, node, buf, len, out_cap, cmd, arg, 0, 0, nullptr }
	{ }
};


/**
 * The nodes as the chardev server sees them: every operation is a request
 * to Linux
 *
 * Readiness is the value Linux keeps up to date, read without a request.
 * Linux announces its changes to the 'Notifier', not to the server: the
 * server's mutex may be held by a session that waits for its request.
 */
struct Pixel_modem::Backend : Android_chardev_server::Backend, Noncopyable
{
	enum { MAX_NODES = 32 };

	Signal_context_capability const _wakeup;

	Mutex    _queue_mutex { };
	Request *_head { nullptr }, *_tail { nullptr };

	struct Lx_node { Node name { }; void *lx { nullptr }; };

	Mutex   _nodes_mutex { };
	Lx_node _nodes[MAX_NODES] { };

	Backend(Backend const &) = delete;
	Backend &operator = (Backend const &) = delete;

	Backend(Signal_context_capability wakeup) : _wakeup(wakeup) { }

	void _execute(Request &r)
	{
		{
			Mutex::Guard guard(_queue_mutex);
			if (_tail) _tail->next = &r; else _head = &r;
			_tail = &r;
		}
		Signal_transmitter(_wakeup).submit();
		r.done.block();
	}

	Request *next_request()
	{
		Mutex::Guard guard(_queue_mutex);

		Request * const r = _head;
		if (r) {
			_head = static_cast<Request *>(r->next);
			if (!_head) _tail = nullptr;
		}
		return r;
	}

	void *_lx_node(Node const &name)
	{
		Mutex::Guard guard(_nodes_mutex);

		for (Lx_node const &n : _nodes)
			if (n.lx && n.name == name)
				return n.lx;
		return nullptr;
	}

	void _remember(Node const &name, void *lx)
	{
		Mutex::Guard guard(_nodes_mutex);

		for (Lx_node &n : _nodes) {
			if (n.lx && n.name == name) return;
			if (!n.lx) {
				n = { name, lx };
				return;
			}
		}
		error("too many device nodes, '", name, "' is never readable");
	}

	static Chardev::Io_result _io_result(Request const &r)
	{
		if (r.would_block) return { Chardev::WOULD_BLOCK, 0, 0 };
		if (r.result < 0)  return { Chardev::ERROR, int32_t(-r.result), 0 };
		return { Chardev::OK, 0, uint32_t(r.result) };
	}

	bool open(Node const &name) override
	{
		Request r { LX_BRIDGE_OPEN, name.string(), nullptr, nullptr, 0, 0, 0, 0 };
		_execute(r);
		if (r.result < 0) {
			if (r.node) error(name, ": open failed: ", r.result);
			return false;
		}
		_remember(name, r.node);
		return true;
	}

	void close(Node const &name) override
	{
		Request r { LX_BRIDGE_CLOSE, name.string(), _lx_node(name), nullptr, 0, 0, 0, 0 };
		if (r.node) _execute(r);
	}

	Chardev::Io_result read(Node const &name, uint8_t *buf, size_t max) override
	{
		Request r { LX_BRIDGE_READ, name.string(), _lx_node(name), buf, max, 0, 0, 0 };
		_execute(r);
		return _io_result(r);
	}

	Chardev::Io_result write(Node const &name, uint8_t const *buf, size_t len) override
	{
		Request r { LX_BRIDGE_WRITE, name.string(), _lx_node(name),
		            const_cast<uint8_t *>(buf), len, 0, 0, 0 };
		_execute(r);
		return _io_result(r);
	}

	Chardev::Ioctl_result ioctl(Node const &name, unsigned long cmd,
	                            unsigned long arg, uint8_t *buf,
	                            size_t in_len, size_t out_cap) override
	{
		Request r { LX_BRIDGE_IOCTL, name.string(), _lx_node(name), buf,
		            in_len, out_cap, cmd, arg };
		_execute(r);

		if (r.result < 0) return { Chardev::ERROR, int32_t(-r.result), 0, 0 };
		return { Chardev::OK, 0, r.result, uint32_t(out_cap) };
	}

	/* a write never waits in the cpif, it fails if the CP's queue is full */
	uint32_t ready(Node const &name) override
	{
		void * const node = _lx_node(name);
		return Chardev::WRITABLE | (node ? lx_bridge_ready(node) : 0);
	}
};


/**
 * Passes the changes of the readiness to the server from a thread of its
 * own, the server is not to be called from inside a backend call
 */
struct Pixel_modem::Notifier : Thread, Noncopyable
{
	Semaphore _sem { };

	bool volatile _pending { false };

	Android_chardev_server::Server &_server;

	Notifier(Env &env, Android_chardev_server::Server &server)
	:
		Thread(env, "chardev_notify", Thread::Stack_size { 16*1024 }),
		_server(server)
	{ }

	void changed()
	{
		if (_pending) return;
		_pending = true;
		_sem.up();
	}

	void entry() override
	{
		for (;;) {
			_sem.down();
			_pending = false;
			_server.backend_changed();
		}
	}
};


struct Pixel_modem::Main : Noncopyable
{
	Env &_env;

	Heap _heap { _env.ram(), _env.rm() };

	Attached_rom_dataspace _dtb_rom { _env, "dtb" };

	Signal_handler<Main> _signal_handler {
		_env.ep(), *this, &Main::_handle_signal };

	void _handle_signal()
	{
		lx_user_handle_io();
		Lx_kit::env().scheduler.execute();
	}

	Backend _backend { _signal_handler };

	Android_chardev_server::Server _server { _env, _heap, _backend };

	Notifier _notifier { _env, _server };

	Signal_handler<Main> _announce_handler {
		_env.ep(), *this, &Main::_handle_announce };

	void _handle_announce() { _server.announce(); }

	Main(Env &env);
};


/* used by the Linux side as soon as the kernel runs */
static Pixel_modem::Main *_main_ptr;


Pixel_modem::Main::Main(Env &env) : _env(env)
{
	log("--- Pixel modem driver ---");

	Lx_kit::initialize(_env, _signal_handler);
	_env.exec_static_constructors();

	Capability<Pd_session::System_control> const cap =
		_env.pd().system_control_cap(Affinity::Location());
	if (!cap.valid())
		error("no system-control capability, the driver needs "
		      "'managing_system: yes'");
	_system_control.construct(cap);

	_notifier.start();

	_main_ptr = this;

	lx_emul_start_kernel(_dtb_rom.local_addr<void>());
}


extern "C" struct lx_bridge_request *genode_bridge_next_request(void)
{
	return _main_ptr->_backend.next_request();
}


extern "C" void genode_bridge_request_done(struct lx_bridge_request *r)
{
	static_cast<Pixel_modem::Request *>(r)->done.wakeup();
}


extern "C" void genode_bridge_changed(void)
{
	_main_ptr->_notifier.changed();
}


extern "C" void genode_bridge_driver_ready(void)
{
	Genode::Signal_transmitter(_main_ptr->_announce_handler).submit();
}


void Component::construct(Genode::Env &env)
{
	static Pixel_modem::Main main(env);
}
