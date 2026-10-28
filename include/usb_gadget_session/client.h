/*
 * \brief  Client of a USB gadget function session
 * \author Ivan Loskutov
 * \date   2026-10-23
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__USB_GADGET_SESSION__CLIENT_H_
#define _INCLUDE__USB_GADGET_SESSION__CLIENT_H_

#include <base/rpc_client.h>
#include <packet_stream_tx/client.h>
#include <usb_gadget_session/usb_gadget_session.h>

namespace Usb_gadget { class Session_client; }


class Usb_gadget::Session_client : public Rpc_client<Session>
{
	private:

		Packet_stream_tx::Client<Tx> _tx;

	public:

		Session_client(Capability<Session> session,
		               Range_allocator    &tx_buffer_alloc,
		               Env::Local_rm      &rm)
		:
			Rpc_client<Session>(session),
			_tx(tx_cap(), rm, tx_buffer_alloc)
		{ }

		bool function(Blob const &fs, Blob const &hs, Blob const &ss,
		              Blob const &strings) override {
			return call<Rpc_function>(fs, hs, ss, strings); }

		Event event() override { return call<Rpc_event>(); }

		void event_sigh(Signal_context_capability sigh) override {
			call<Rpc_event_sigh>(sigh); }

		Tx *tx_channel() override { return &_tx; }

		Tx::Source *tx() override { return _tx.source(); }

		Capability<Tx> tx_cap() override { return call<Rpc_tx_cap>(); }

		/**
		 * Packet of 'size' bytes for 'endpoint', invalid if the buffer is full
		 *
		 * Packets start on a cache line of their own (ALIGN_LOG2), so that
		 * the driver's cache maintenance for one packet's DMA never touches
		 * another's bytes.
		 */
		Packet_descriptor alloc_packet(size_t size, uint8_t endpoint)
		{
			return tx()->alloc_packet_attempt(size, ALIGN_LOG2).convert<Packet_descriptor>(
				[&] (Packet_descriptor p) { p.endpoint = endpoint; return p; },
				[&] (auto) { return Packet_descriptor(); });
		}
};

#endif /* _INCLUDE__USB_GADGET_SESSION__CLIENT_H_ */
