/*
 * \brief  Android_chardev server for one set of device nodes
 * \author Ivan Loskutov
 * \date   2026-09-28
 *
 * 'Root_component' finds a session to close on its own entrypoint only, so
 * the root implements 'Typed_root' itself and looks the session up by its
 * capability.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/attached_ram_dataspace.h>
#include <base/log.h>
#include <base/quota_guard.h>
#include <base/ram_allocator.h>
#include <base/rpc_server.h>
#include <os/session_policy.h>
#include <root/root.h>
#include <util/reconstructible.h>

#include <android_chardev_server/server.h>

using namespace Android_chardev_server;


/*
 * 'Constructible<Rpc_entrypoint>' would call the base-object constructor of
 * 'Rpc_entrypoint', which ld.lib.so does not export
 */
struct Android_chardev_server::Ep
{
	Rpc_entrypoint ep;

	Ep(Env &env, char const *name)
	:
		ep(env.runtime(), name, Thread::Stack_size { 16*1024 }, Affinity::Location())
	{ }
};


class Android_chardev_server::Session_component : public Rpc_object<Session>
{
	private:

		friend class Server;
		friend class Root;

		enum { EINVAL_ = 22 };

		Server                          &_server;
		Node const                       _node;
		Constructible<Ep>           _ep { };
		List_element<Session_component>  _le { this };

		/* the buffer is paid by the session's quota */
		Ram_quota_guard                  _ram_guard;
		Cap_quota_guard                  _cap_guard;
		Accounted_ram_allocator          _ram { _server._env.ram(), _ram_guard, _cap_guard };
		Attached_ram_dataspace           _ds;

		/* guarded by the server's mutex */
		Signal_context_capability _sigh { };
		uint32_t                  _reported = Session::WRITABLE;

		uint8_t *_buf() { return _ds.local_addr<uint8_t>(); }

		Backend &_backend() { return _server._backend; }

		/**
		 * Signal the client if its readiness changed (mutex held)
		 */
		void _update_readiness()
		{
			uint32_t const now = _backend().ready(_node);
			if (now == _reported)
				return;

			_reported = now;
			if (_sigh.valid())
				Signal_transmitter(_sigh).submit();
		}

		Io_result _too_large(char const *op, uint32_t len)
		{
			error(_node, ": ", op, " of ", len, " bytes exceeds the buffer");
			return { Session::ERROR, EINVAL_, 0 };
		}

	public:

		/**
		 * \throw Out_of_ram, Out_of_caps  the quota does not cover the buffer
		 */
		Session_component(Server &server, Node const &node,
		                  Ram_quota ram_quota, Cap_quota cap_quota)
		:
			_server(server), _node(node),
			_ram_guard(ram_quota), _cap_guard(cap_quota),
			_ds(_ram, server._env.rm(), Session::BUFFER_SIZE)
		{
			_ep.construct(server._env, String<32>("chardev_", node).string());
			_ep->ep.manage(this);
		}

		void upgrade(Ram_quota ram, Cap_quota caps)
		{
			_ram_guard.upgrade(ram);
			_cap_guard.upgrade(caps);
		}

		~Session_component()
		{
			/*
			 * 'dissolve' does not wait for an RPC still running, the
			 * destruction of the entrypoint does
			 */
			_ep->ep.dissolve(this);
			_ep.destruct();

			Mutex::Guard guard(_server._mutex);
			_server._sessions.remove(&_le);
			_backend().close(_node);
			_server._update_readiness();
		}

		Dataspace_capability dataspace() override { return _ds.cap(); }

		Io_result write(uint32_t len) override
		{
			if (len > Session::BUFFER_SIZE)
				return _too_large("write", len);

			Mutex::Guard guard(_server._mutex);
			Io_result const r = _backend().write(_node, _buf(), len);
			_server._update_readiness();
			return r;
		}

		Io_result read(uint32_t max) override
		{
			if (max > Session::BUFFER_SIZE)
				max = Session::BUFFER_SIZE;

			Mutex::Guard guard(_server._mutex);
			Io_result const r = _backend().read(_node, _buf(), max);

			/* an empty read changes nothing, its signal would wake the reader */
			if (r.result != Session::WOULD_BLOCK)
				_server._update_readiness();
			return r;
		}

		Ioctl_result ioctl(uint64_t cmd, uint64_t arg,
		                   uint32_t in_len, uint32_t out_cap) override
		{
			if (in_len > Session::BUFFER_SIZE || out_cap > Session::BUFFER_SIZE) {
				error(_node, ": ioctl with ", in_len, " input and ", out_cap,
				      " output bytes exceeds the buffer");
				return { Session::ERROR, EINVAL_, 0, 0 };
			}

			/* may block, not serialized */
			Ioctl_result const r = _backend().ioctl(_node, (unsigned long)cmd,
			                                        (unsigned long)arg, _buf(),
			                                        in_len, out_cap);
			Mutex::Guard guard(_server._mutex);
			_server._update_readiness();
			return r;
		}

		uint32_t ready() override
		{
			Mutex::Guard guard(_server._mutex);
			return _backend().ready(_node);
		}

		void readiness_sigh(Signal_context_capability sigh) override
		{
			Mutex::Guard guard(_server._mutex);
			_sigh = sigh;
		}
};


class Android_chardev_server::Root : public Rpc_object<Typed_root<Session>>
{
	private:

		Server &_server;

		Result _denied(Node const &node, Session_error e)
		{
			Mutex::Guard guard(_server._mutex);
			_server._backend.close(node);
			return e;
		}

		Session_component *_lookup(Session_capability cap)
		{
			Mutex::Guard guard(_server._mutex);
			for (auto *e = _server._sessions.first(); e; e = e->next())
				if (e->object()->cap().local_name() == cap.local_name())
					return e->object();
			return nullptr;
		}

	public:

		Root(Server &server) : _server(server) { }

		Result session(Session_args const &args, Affinity const &) override
		{
			if (!args.valid_string())
				return Session_error::DENIED;

			Session_label const label = session_label_from_args(args.string());
			Node          const node  = label.last_element();

			/*
			 * The client's policy lists the nodes it may open: the daemons
			 * differ in what they are for (cbd boots the CP, rild talks to
			 * it), a session of the wrong node could reset or crash the CP.
			 */
			if (!_server._allowed(label, node)) {
				unsigned times = 0;
				{
					Mutex::Guard guard(_server._mutex);
					times = _server._count_denied(node);
				}
				if (times)
					log("no policy for '", label, "', session denied (", times, " times)");
				return Session_error::DENIED;
			}

			bool     opened = false;
			unsigned times  = 0;
			{
				Mutex::Guard guard(_server._mutex);
				opened = _server._backend.open(node);
				if (!opened)
					times = _server._count_denied(node);
			}
			if (!opened) {
				if (times)
					log("no node '", node, "', session denied (", times, " times)");
				return Session_error::DENIED;
			}

			Session_component *s = nullptr;
			try {
				s = new (_server._alloc)
					Session_component(_server, node, ram_quota_from_args(args.string()),
					                  cap_quota_from_args(args.string()));
			}
			catch (Out_of_ram)  { return _denied(node, Session_error::INSUFFICIENT_RAM); }
			catch (Out_of_caps) { return _denied(node, Session_error::INSUFFICIENT_CAPS); }
			catch (...)         { return _denied(node, Session_error::DENIED); }

			Mutex::Guard guard(_server._mutex);
			_server._sessions.insert(&s->_le);
			_server._update_readiness();
			return Session_capability(s->cap());
		}

		void upgrade(Session_capability cap, Upgrade_args const &args) override
		{
			if (!args.valid_string()) return;
			if (Session_component *s = _lookup(cap))
				s->upgrade(ram_quota_from_args(args.string()),
				           cap_quota_from_args(args.string()));
		}

		void close(Session_capability cap) override
		{
			if (Session_component *s = _lookup(cap))
				destroy(_server._alloc, s);
		}
};


/**
 * The matching '<policy>' of the client names 'node' by a '<node name>' or
 * a '<node prefix>'
 */
bool Server::_allowed(Session_label const &label, Node const &node)
{
	bool allowed = false;
	_config.update();
	with_matching_policy(label, _config.node(),
		[&] (Genode::Node const &policy) {
			policy.for_each_sub_node("node", [&] (Genode::Node const &n) {
				if (n.attribute_value("name", Node()) == node)
					allowed = true;
				Node const prefix = n.attribute_value("prefix", Node());
				if (prefix.length() > 1
				 && Genode::strcmp(prefix.string(), node.string(), prefix.length() - 1) == 0)
					allowed = true;
			});
		},
		[&] { });
	return allowed;
}


/**
 * Count a denied request of 'node'
 *
 * \return  number of requests so far if this one is to be logged (the
 *          first, then every 64th), 0 otherwise
 */
unsigned Server::_count_denied(Node const &node)
{
	auto logged = [] (unsigned count) { return count % 64 == 1 ? count : 0; };

	for (Denied &d : _denied) {
		if (d.count && d.node == node)
			return logged(++d.count);

		if (!d.count) {
			d = { node, 1 };
			return 1;
		}
	}
	return 0;   /* more names than entries, not logged */
}


void Server::_update_readiness()
{
	for (auto *e = _sessions.first(); e; e = e->next())
		e->object()->_update_readiness();
}


void Server::backend_changed()
{
	Mutex::Guard guard(_mutex);
	_update_readiness();
}


void Server::announce()
{
	_env.parent().announce(_root_ep.ep.manage(&_root));
}


Server::Server(Env &env, Allocator &alloc, Backend &backend)
:
	_env(env), _alloc(alloc), _backend(backend),
	_root_ep(*new (alloc) Ep(env, "chardev_root")),
	_root(*new (alloc) Root(*this))
{ }


Server::~Server()
{
	_root_ep.ep.dissolve(&_root);

	while (auto *e = _sessions.first())
		destroy(_alloc, e->object());

	destroy(_alloc, &_root);
	destroy(_alloc, &_root_ep);
}
