/*
 * \brief  Interface between the Linux gadget code and the Genode sessions
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * session.cc (Genode side) serves the Usb_gadget sessions, gadget.c (Linux
 * side) drives the UDC. The Linux side calls the 'gadget_*' functions of
 * session.cc; all calls happen on the component's entrypoint (Lx_kit
 * tasks and RPCs never run at the same time).
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _USB_GADGET__GADGET_API_H_
#define _USB_GADGET__GADGET_API_H_

#ifdef __cplusplus
extern "C" {
#endif

enum { GADGET_MAX_SLOTS = 4, GADGET_MAX_EPS = 15, GADGET_MAX_STRINGS = 8,
       GADGET_BLOB_MAX = 1024 };

enum gadget_speed { GADGET_FS = 0, GADGET_HS = 1, GADGET_SS = 2 };

enum gadget_status { GADGET_OK = 1, GADGET_FLUSHED = 2, GADGET_HALTED = 3,
                     GADGET_STALL = 4 };

enum gadget_event_type { GADGET_ENABLED = 1, GADGET_DISABLED = 2,
                         GADGET_SUSPEND = 3, GADGET_RESUME = 4,
                         GADGET_SETUP = 5 };

struct gadget_identity
{
	unsigned short vendor_id, product_id;
	unsigned       max_power_ma;
	char const    *manufacturer, *product, *serial;
};

struct gadget_packet
{
	unsigned long tag;      /* for gadget_ack */
	unsigned      endpoint; /* function-local, 0 = control */
	void         *buf;      /* in the session's DMA buffer */
	unsigned      len;
	int           zlp;
	int           stall;
};

/* identity from the config, number of <function> slots */
void     gadget_identity(struct gadget_identity *);
unsigned gadget_slot_count(void);

/* the slot's session has described its function */
int gadget_slot_ready(unsigned slot);

/* number of strings of the slot's function, string i (from 0) */
unsigned    gadget_string_count(unsigned slot);
char const *gadget_string(unsigned slot, unsigned i);

/*
 * The slot's descriptors for 'speed', placed: interface and string numbers
 * get a base, the endpoint addresses are those the UDC assigned (local
 * endpoint i+1 -> 'ep_address[i]'). Returns the length written to 'out'
 * (GADGET_BLOB_MAX), 0 on error.
 */
unsigned gadget_compose(unsigned slot, enum gadget_speed speed,
                        unsigned char interface_base, unsigned char string_base,
                        unsigned char const *ep_address, unsigned ep_count,
                        unsigned char *out);

/* raw descriptors of the slot for 'speed' (function-local numbers) */
unsigned char const *gadget_descriptors(unsigned slot, enum gadget_speed speed,
                                        unsigned *len);

/* take the next submitted packet of the slot, 1 if there was one */
int gadget_next_packet(unsigned slot, struct gadget_packet *);

/* acknowledge a packet taken with gadget_next_packet */
void gadget_ack(unsigned slot, unsigned long tag, enum gadget_status, unsigned actual);

/* queue an event for the slot's function, 'setup' for GADGET_SETUP */
void gadget_event(unsigned slot, enum gadget_event_type, enum gadget_speed,
                  unsigned char const *setup);

/*
 * The Linux side has no request on the slot's buffer any more (gadget
 * unbound): the buffer of a closed session can go
 */
void gadget_slot_released(unsigned slot);

/* implemented by gadget.c: wake the Linux task that serves the sessions */
void gadget_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif /* _USB_GADGET__GADGET_API_H_ */
