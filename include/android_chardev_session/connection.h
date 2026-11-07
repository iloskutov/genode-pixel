/*
 * \brief  Connection to an Android_chardev server
 * \author Ivan Loskutov
 * \date   2026-11-07
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__ANDROID_CHARDEV_SESSION__CONNECTION_H_
#define _INCLUDE__ANDROID_CHARDEV_SESSION__CONNECTION_H_

#include <base/connection.h>
#include <android_chardev_session/android_chardev_session.h>

namespace Android_chardev_session { struct Connection; }


struct Android_chardev_session::Connection : Genode::Connection<Session>,
                                             Session_client
{
	/**
	 * \param label  name of the device node, e.g. "umts_boot0"
	 */
	Connection(Genode::Env &env, Genode::Session_label const &label)
	:
		Genode::Connection<Session>(env, label,
		                            Genode::Ram_quota { RAM_QUOTA }, Args()),
		Session_client(cap())
	{ }
};

#endif /* _INCLUDE__ANDROID_CHARDEV_SESSION__CONNECTION_H_ */
