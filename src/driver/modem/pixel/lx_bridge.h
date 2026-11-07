/*
 * \brief  Interface between the Linux part (lx_user.c) and the Genode part (main.cc)
 * \author Ivan Loskutov
 * \date   2026-11-07
 *
 * All Linux code runs cooperatively on the component's entrypoint. The
 * Android_chardev sessions run on entrypoints of their own: they post a
 * request, wake up the entrypoint and wait until a Linux worker task
 * executed it.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _LX_BRIDGE_H_
#define _LX_BRIDGE_H_

#ifdef __cplusplus
extern "C" {
#endif

enum lx_bridge_op {
	LX_BRIDGE_OPEN, LX_BRIDGE_CLOSE, LX_BRIDGE_READ, LX_BRIDGE_WRITE, LX_BRIDGE_IOCTL };

enum { LX_BRIDGE_READABLE = 1, LX_BRIDGE_HANGUP = 4 };

struct lx_bridge_request
{
	enum lx_bridge_op op;

	char const *name;           /* in:     node name (OPEN) */
	void       *node;           /* in/out: result of OPEN, input of all others */
	void       *buf;            /* session buffer */
	unsigned long len;          /* READ/WRITE: size, IOCTL: input bytes in 'buf' */
	unsigned long out_cap;      /* IOCTL: output bytes expected in 'buf' */
	unsigned long cmd, arg;     /* IOCTL */

	long result;                /* out: Linux return value (negative errno) */
	int  would_block;           /* out: READ found the queue empty */

	struct lx_bridge_request *next;
};


/*
 * Implemented in lx_user.c
 */

/* READABLE | HANGUP of a node, callable from any thread */
unsigned lx_bridge_ready(void *node);


/*
 * Implemented in main.cc, called from Linux tasks or lx_user_handle_io
 */

struct lx_bridge_request *genode_bridge_next_request(void);
void genode_bridge_request_done(struct lx_bridge_request *);

/* the readiness of a node changed */
void genode_bridge_changed(void);

/* the device nodes exist: announce the service */
void genode_bridge_driver_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* _LX_BRIDGE_H_ */
