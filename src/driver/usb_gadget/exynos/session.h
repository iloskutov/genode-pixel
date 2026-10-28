/*
 * \brief  Usb_gadget sessions of the gadget driver
 * \author Ivan Loskutov
 * \date   2026-10-23
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _USB_GADGET__SESSION_H_
#define _USB_GADGET__SESSION_H_

#include <base/env.h>
#include <base/allocator.h>
#include <base/node.h>

/**
 * Announce the service, slots and identity from 'config'
 *
 * \param sigh  driver signal handler: runs the Linux side
 */
void usb_gadget_sessions_init(Genode::Env &, Genode::Allocator &,
                              Genode::Node const &config,
                              Genode::Signal_context_capability sigh);

#endif /* _USB_GADGET__SESSION_H_ */
