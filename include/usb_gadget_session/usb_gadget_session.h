/*
 * \brief  Session of a USB gadget function (device side of USB)
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * One session per function of the gadget (e.g. CDC ACM, adb), in the
 * spirit of Linux' functionfs without files. The function hands over its
 * descriptors (local numbers, see usb_gadget/descriptor.h), then moves
 * data with packets: a packet names a function-local endpoint (0 = its
 * control endpoint, 1.. = endpoints in descriptor order) and comes back
 * acknowledged with status and actual length.
 *
 * IN (to the host): the packet carries the data; 'zlp' ends a transfer of
 * a max-packet multiple with a zero-length packet.
 * OUT (from the host): the packet is an empty buffer of the requested
 * size; it is acknowledged with 'actual' <= size (a short packet ends it).
 * Endpoint 0: a SETUP event carries a class or vendor request addressed to
 * the function; the function answers with one packet on endpoint 0: the
 * data stage (IN), a buffer for the data stage (OUT), zero length for the
 * status stage only, or 'stall'.
 * On DISABLED (bus reset, deconfigure, disconnect) all packets in flight
 * are acknowledged FLUSHED before the event is queued; packets submitted
 * while the function is not enabled are acknowledged FLUSHED at once. The
 * two arrive through different signals, so a client takes all available
 * acknowledgements before it reads events.
 *
 * Isochronous endpoints (not implemented yet) will carry 'frames' entries
 * of '{u32 size, u32 actual_size}' after a 'u32 number_of_frames' in front
 * of the payload, as the host-side Usb session.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__USB_GADGET_SESSION__USB_GADGET_SESSION_H_
#define _INCLUDE__USB_GADGET_SESSION__USB_GADGET_SESSION_H_

#include <base/rpc_args.h>
#include <os/packet_stream.h>
#include <packet_stream_tx/packet_stream_tx.h>
#include <session/session.h>

namespace Usb_gadget {

	using namespace Genode;

	struct Packet_descriptor;
	struct Event;
	struct Session;
}


struct Usb_gadget::Packet_descriptor : Genode::Packet_descriptor
{
	enum Status : uint8_t { PENDING, OK, FLUSHED, HALTED, STALL };

	uint8_t  endpoint = 0;
	bool     zlp      = false;
	bool     stall    = false;
	Status   status   = PENDING;
	uint32_t actual   = 0;
	uint32_t frames   = 0;

	using Genode::Packet_descriptor::Packet_descriptor;
};


struct Usb_gadget::Event
{
	enum Type : uint8_t { NONE, ENABLED, DISABLED, SUSPEND, RESUME, SETUP };
	enum Speed : uint8_t { FULL, HIGH, SUPER, SUPER_PLUS };

	Type    type     = NONE;
	Speed   speed    = FULL;
	uint8_t setup[8] { };   /* SETUP: the request, wIndex function-local */
};


struct Usb_gadget::Session : Genode::Session
{
	/**
	 * \noapi
	 */
	static char const *service_name() { return "Usb_gadget"; }

	/*
	 * ALIGN_LOG2: packets with data start at 128-byte boundaries (arm64's
	 * DMA cache-line size); the driver refuses others
	 */
	enum { CAP_QUOTA = 6, TX_QUEUE_SIZE = 64, BLOB_MAX = 512, ALIGN_LOG2 = 7 };

	using Tx_policy = Packet_stream_policy<Packet_descriptor, TX_QUEUE_SIZE,
	                                       TX_QUEUE_SIZE, char>;
	using Tx        = Packet_stream_tx::Channel<Tx_policy>;
	using Blob      = Rpc_in_buffer<BLOB_MAX>;

	virtual ~Session() { }

	/**
	 * Describe the function
	 *
	 * \param fs, hs, ss  descriptors for full, high and super speed
	 * \param strings     NUL-terminated strings, index 1 first
	 *
	 * \return false if the descriptors are broken
	 */
	virtual bool function(Blob const &fs, Blob const &hs, Blob const &ss,
	                      Blob const &strings) = 0;

	/**
	 * Next event, type NONE if there is none
	 */
	virtual Event event() = 0;

	/**
	 * Signal handler for new events
	 */
	virtual void event_sigh(Signal_context_capability) = 0;

	/**
	 * Packet stream of the client side, nullptr on the server side
	 */
	virtual Tx *tx_channel() { return nullptr; }
	virtual Tx::Source *tx() { return nullptr; }

	virtual Capability<Tx> tx_cap() = 0;


	/*******************
	 ** RPC interface **
	 *******************/

	GENODE_RPC(Rpc_function, bool, function, Blob const &, Blob const &,
	           Blob const &, Blob const &);
	GENODE_RPC(Rpc_event, Event, event);
	GENODE_RPC(Rpc_event_sigh, void, event_sigh, Signal_context_capability);
	GENODE_RPC(Rpc_tx_cap, Capability<Tx>, tx_cap);
	GENODE_RPC_INTERFACE(Rpc_function, Rpc_event, Rpc_event_sigh, Rpc_tx_cap);
};

#endif /* _INCLUDE__USB_GADGET_SESSION__USB_GADGET_SESSION_H_ */
