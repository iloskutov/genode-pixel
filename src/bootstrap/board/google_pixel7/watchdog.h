/*
 * \brief   Watchdog timers of the Google Pixel 7 at the hand-off to bootstrap
 * \author  Ivan Loskutov
 * \date    2026-10-01
 *
 * The register layout is taken from the Linux driver
 * drivers/watchdog/s3c2410_wdt.c.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _BOOTSTRAP__BOARD__GOOGLE_PIXEL7__WATCHDOG_H_
#define _BOOTSTRAP__BOARD__GOOGLE_PIXEL7__WATCHDOG_H_

#include <cpu/mmio_access.h>

namespace Watchdog {

	enum : Genode::addr_t {
		CLUSTER0_BASE = 0x10060000,
		CLUSTER1_BASE = 0x10070000,
		WTCON         = 0x0,
	};

	/**
	 * Stop the watchdog timers of both clusters
	 *
	 * The boot loader hands over with a watchdog of about 60 s. The timer of
	 * cluster 1 fires first: an FIQ routed to the secure world, which tries
	 * to save a debug snapshot and hangs the system instead of resetting it.
	 * Nothing in Genode services the timers.
	 *
	 * Called before the MMU is enabled.
	 */
	static inline void stop()
	{
		Hw::mmio_write<Genode::uint32_t>(CLUSTER0_BASE + WTCON, 0);
		Hw::mmio_write<Genode::uint32_t>(CLUSTER1_BASE + WTCON, 0);
	}
}

#endif /* _BOOTSTRAP__BOARD__GOOGLE_PIXEL7__WATCHDOG_H_ */
