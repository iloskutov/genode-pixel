/*
 * \brief  Android_chardev server for one set of device nodes
 * \author Ivan Loskutov
 * \date   2026-09-28
 *
 * Every session and the root run on an entrypoint of their own: an ioctl may
 * block for a long time (the CP's boot), and a client asks for the state of
 * the modem on a second session meanwhile. The component's entrypoint stays
 * free for the driver.
 *
 * The backend calls other than 'ioctl' are serialized by the server. An
 * ioctl runs concurrently with the other calls, the backend makes it
 * thread-safe. Readiness is compared after every operation and on
 * 'backend_changed()', and signalled to the sessions whose readiness
 * changed.
 *
 * The component's config decides which client opens which node:
 *
 * ! + policy | label_prefix: cbd
 * !   + node | prefix: umts_boot
 * ! + policy | label_prefix: rild
 * !   + node | name: umts_ipc0
 * !   + node | prefix: oem_
 *
 * A client without a matching policy, or one whose policy does not name
 * the node, is denied.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__ANDROID_CHARDEV_SERVER__SERVER_H_
#define _INCLUDE__ANDROID_CHARDEV_SERVER__SERVER_H_

#include <base/attached_rom_dataspace.h>
#include <base/env.h>
#include <base/mutex.h>
#include <base/session_label.h>
#include <util/list.h>
#include <android_chardev_session/android_chardev_session.h>

namespace Android_chardev_server {

	using namespace Genode;

	using Session = Android_chardev_session::Session;
	using Node    = String<24>;

	struct Backend;
	class  Server;
	class  Session_component;
	class  Root;
	struct Ep;
}


struct Android_chardev_server::Backend : Interface
{
	/**
	 * \return false if there is no node of this name, the session is denied
	 */
	virtual bool open(Node const &) = 0;

	virtual void close(Node const &) = 0;

	virtual Session::Io_result read(Node const &, uint8_t *buf, size_t max) = 0;

	virtual Session::Io_result write(Node const &, uint8_t const *buf, size_t len) = 0;

	/**
	 * \param arg      value argument of a command without a size
	 * \param buf      'in_len' input bytes, receives the output bytes
	 * \param out_cap  output bytes the client expects
	 */
	virtual Session::Ioctl_result ioctl(Node const &, unsigned long cmd,
	                                    unsigned long arg, uint8_t *buf,
	                                    size_t in_len, size_t out_cap) = 0;

	/**
	 * READABLE | WRITABLE | HANGUP of the node, must not block
	 */
	virtual uint32_t ready(Node const &) = 0;
};


class Android_chardev_server::Server : Noncopyable
{
	private:

		friend class Session_component;
		friend class Root;

		Env       &_env;
		Allocator &_alloc;
		Backend   &_backend;

		Attached_rom_dataspace _config { _env, "config" };

		bool _allowed(Session_label const &, Node const &);

		Mutex _mutex { };   /* backend calls but ioctl, session list */

		List<List_element<Session_component>> _sessions { };

		/* denied session requests per node, a daemon may retry in a loop */
		struct Denied { Node node; unsigned count; };

		Denied _denied[16] { };

		unsigned _count_denied(Node const &);

		Ep      &_root_ep;
		Root    &_root;

		/**
		 * Signal every session whose readiness changed (mutex held)
		 */
		void _update_readiness();

	public:

		Server(Env &, Allocator &, Backend &);

		~Server();

		/**
		 * Announce the service at the parent
		 */
		void announce();

		/**
		 * Compare the readiness of all sessions again
		 *
		 * Must not be called from inside a backend call.
		 */
		void backend_changed();
};

#endif /* _INCLUDE__ANDROID_CHARDEV_SERVER__SERVER_H_ */
