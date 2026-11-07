/*
 * \brief  Android_chardev session: one open file of a Linux character device
 * \author Ivan Loskutov
 * \date   2026-09-26
 *
 * The last element of the session label names the node ("umts_boot0"). Data
 * travels in the session's buffer. The server never blocks a read or a
 * write: an empty read or a full write returns WOULD_BLOCK, and a change of
 * the readiness is signalled. An ioctl may block. Errors are Linux errno
 * values, the clients emulate Linux.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__ANDROID_CHARDEV_SESSION__ANDROID_CHARDEV_SESSION_H_
#define _INCLUDE__ANDROID_CHARDEV_SESSION__ANDROID_CHARDEV_SESSION_H_

#include <base/rpc_client.h>
#include <base/signal.h>
#include <dataspace/capability.h>
#include <session/session.h>

namespace Android_chardev_session {

	using namespace Genode;

	struct Session;
	struct Session_client;
}


struct Android_chardev_session::Session : Genode::Session
{
	static char const *service_name() { return "Android_chardev"; }

	enum { CAP_QUOTA   = 4,
	       BUFFER_SIZE = 128*1024,
	       RAM_QUOTA   = BUFFER_SIZE + 16*1024 };

	enum Result : int32_t  { OK = 0, WOULD_BLOCK = 1, ERROR = 2 };
	enum Ready  : uint32_t { READABLE = 1, WRITABLE = 2, HANGUP = 4 };

	struct Io_result    { int32_t result; int32_t err; uint32_t count; };
	struct Ioctl_result { int32_t result; int32_t err; int64_t ret; uint32_t out_len; };

	virtual Dataspace_capability dataspace() = 0;

	/**
	 * Write 'len' bytes from the buffer
	 */
	virtual Io_result write(uint32_t len) = 0;

	/**
	 * Read at most 'max' bytes into the buffer
	 */
	virtual Io_result read(uint32_t max) = 0;

	/**
	 * \param arg      value argument of a command without a size
	 * \param in_len   input bytes at the start of the buffer
	 * \param out_cap  output bytes expected back at the start of the buffer
	 */
	virtual Ioctl_result ioctl(uint64_t cmd, uint64_t arg,
	                           uint32_t in_len, uint32_t out_cap) = 0;

	/**
	 * READABLE | WRITABLE | HANGUP
	 *
	 * Never blocks. READABLE means that the next read does not return
	 * WOULD_BLOCK, unless another reader comes first, WRITABLE likewise.
	 */
	virtual uint32_t ready() = 0;

	/**
	 * Signal on a change of 'ready()'
	 *
	 * Sent on changes only, never after a read that returned WOULD_BLOCK.
	 * A client reads 'ready()' again after a signal and after its own
	 * operations.
	 */
	virtual void readiness_sigh(Signal_context_capability) = 0;

	GENODE_RPC(Rpc_dataspace, Dataspace_capability, dataspace);
	GENODE_RPC(Rpc_write, Io_result, write, uint32_t);
	GENODE_RPC(Rpc_read,  Io_result, read,  uint32_t);
	GENODE_RPC(Rpc_ioctl, Ioctl_result, ioctl, uint64_t, uint64_t, uint32_t, uint32_t);
	GENODE_RPC(Rpc_ready, uint32_t, ready);
	GENODE_RPC(Rpc_readiness_sigh, void, readiness_sigh, Signal_context_capability);

	GENODE_RPC_INTERFACE(Rpc_dataspace, Rpc_write, Rpc_read, Rpc_ioctl,
	                     Rpc_ready, Rpc_readiness_sigh);
};


struct Android_chardev_session::Session_client : Rpc_client<Session>
{
	explicit Session_client(Capability<Session> cap) : Rpc_client<Session>(cap) { }

	Dataspace_capability dataspace() override { return call<Rpc_dataspace>(); }

	Io_result write(uint32_t len) override { return call<Rpc_write>(len); }
	Io_result read (uint32_t max) override { return call<Rpc_read>(max); }

	Ioctl_result ioctl(uint64_t cmd, uint64_t arg,
	                   uint32_t in_len, uint32_t out_cap) override {
		return call<Rpc_ioctl>(cmd, arg, in_len, out_cap); }

	uint32_t ready() override { return call<Rpc_ready>(); }

	void readiness_sigh(Signal_context_capability sigh) override {
		call<Rpc_readiness_sigh>(sigh); }
};

#endif /* _INCLUDE__ANDROID_CHARDEV_SESSION__ANDROID_CHARDEV_SESSION_H_ */
