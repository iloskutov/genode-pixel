/*
 * \brief  Connection to a USB gadget driver for one function
 * \author Ivan Loskutov
 * \date   2026-10-23
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__USB_GADGET_SESSION__CONNECTION_H_
#define _INCLUDE__USB_GADGET_SESSION__CONNECTION_H_

#include <base/connection.h>
#include <usb_gadget_session/client.h>

namespace Usb_gadget { struct Connection; }


struct Usb_gadget::Connection : Genode::Connection<Session>, Session_client
{
	enum { DEFAULT_TX_BUF = 256*1024 };

	Connection(Env &env, Range_allocator &tx_alloc,
	           Label const &label = Label(),
	           size_t tx_buf_size = DEFAULT_TX_BUF)
	:
		Genode::Connection<Session>(env, label,
		                            Ram_quota { tx_buf_size + 8*1024 },
		                            Args("tx_buf_size=", tx_buf_size)),
		Session_client(cap(), tx_alloc, env.rm())
	{ }
};

#endif /* _INCLUDE__USB_GADGET_SESSION__CONNECTION_H_ */
