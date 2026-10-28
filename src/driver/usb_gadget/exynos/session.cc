/*
 * \brief  Usb_gadget sessions of the gadget driver (Genode side)
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * One session per <function> slot of the config, matched by the last
 * element of the session label. A session's packet buffer is DMA memory
 * of lx_emul, so the controller moves data in and out of the client's
 * packets directly. gadget.c (Linux side) takes the packets and turns
 * them into USB requests, see gadget.h.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <base/session_label.h>
#include <packet_stream_tx/rpc_object.h>
#include <root/component.h>
#include <usb_gadget/descriptor.h>
#include <usb_gadget_session/usb_gadget_session.h>

#include <lx_emul/shared_dma_buffer.h>

#include <gadget_api.h>
#include <session.h>

namespace Usb_gadget_driver {

	using namespace Genode;

	struct Session_component;
	struct Root;

	using Packet = Usb_gadget::Packet_descriptor;
	using Label  = String<64>;
	using Text   = String<64>;
}


struct Usb_gadget_driver::Session_component
:
	Rpc_object<Usb_gadget::Session, Session_component>
{
	enum { BLOB_MAX = Usb_gadget::Session::BLOB_MAX, MAX_EVENTS = 32 };

	struct Blob_copy
	{
		unsigned char data[BLOB_MAX];
		unsigned      len = 0;
	};

	unsigned const _slot;

	Signal_context_capability const _driver_sigh;

	genode_shared_dataspace * const _ds;
	char                    * const _dma_base;

	Packet_stream_tx::Rpc_object<Tx> _tx;

	bool      _described = false;
	Blob_copy _blob[3] { };   /* full, high, super speed */

	char        _strings[BLOB_MAX] { };
	char const *_string[GADGET_MAX_STRINGS] { };
	unsigned    _string_count = 0;

	Usb_gadget::Event _events[MAX_EVENTS] { };
	unsigned          _ev_head = 0, _ev_count = 0;

	Signal_context_capability _event_sigh { };

	/* packets taken by the Linux side, the index is the tag */
	Packet   _taken[TX_QUEUE_SIZE] { };
	bool     _taken_used[TX_QUEUE_SIZE] { };
	unsigned _taken_count = 0;

	bool _misaligned_logged = false;

	Session_component(Session_component const &) = delete;
	Session_component &operator = (Session_component const &) = delete;

	Session_component(Env &env, unsigned slot, size_t tx_buf_size,
	                  Signal_context_capability driver_sigh)
	:
		_slot(slot), _driver_sigh(driver_sigh),
		_ds(lx_emul_shared_dma_buffer_allocate(tx_buf_size)),
		_dma_base((char *)lx_emul_shared_dma_buffer_virt_addr(_ds)),
		_tx(genode_shared_dataspace_capability(_ds), env.rm(), env.ep().rpc_ep())
	{
		_tx.sigh_packet_avail(driver_sigh);
		_tx.sigh_ready_to_ack(driver_sigh);
	}

	/*
	 * The DMA buffer outlives the session: the Linux side may still have
	 * requests on it, it gives the slot free with 'gadget_slot_released'
	 */
	genode_shared_dataspace *ds() const { return _ds; }

	unsigned slot() const { return _slot; }

	bool described() const { return _described; }

	void push(Usb_gadget::Event const &e)
	{
		if (_ev_count == MAX_EVENTS) {
			error("function ", _slot, ": event queue full, event dropped");
			return;
		}
		_events[(_ev_head + _ev_count++) % MAX_EVENTS] = e;
		if (_event_sigh.valid())
			Signal_transmitter(_event_sigh).submit();
	}

	Blob_copy const &blob(gadget_speed speed) const { return _blob[speed]; }

	unsigned    string_count() const { return _string_count; }
	char const *string(unsigned i) const { return i < _string_count ? _string[i] : ""; }

	bool next_packet(gadget_packet &out)
	{
		auto &sink = *_tx.sink();

		for (;;) {
			/*
			 * Every taken packet has its ack slot: a reset may complete all
			 * of them at once, and a full ack queue would throw
			 */
			if (!sink.packet_avail() || sink.ack_slots_free() <= _taken_count)
				return false;

			Packet const p = sink.get_packet();

			bool const invalid = p.size() && !sink.packet_valid(p);

			/* DMA of data packets needs cache lines of their own */
			bool const misaligned = p.size() && p.endpoint
			                     && (p.offset() & ((1u << ALIGN_LOG2) - 1));
			if (misaligned && !_misaligned_logged) {
				error("function ", _slot, ": packet not aligned to ",
				      1u << ALIGN_LOG2, " bytes, refused");
				_misaligned_logged = true;
			}

			if (invalid || misaligned) {
				Packet bad = p;
				bad.status = Packet::FLUSHED;
				sink.acknowledge_packet(bad);
				continue;
			}

			if (_take(p, out))
				return true;
		}
	}

	bool _take(Packet const &p, gadget_packet &out)
	{
		unsigned tag = 0;
		for (; tag < TX_QUEUE_SIZE && _taken_used[tag]; tag++) ;
		if (tag == TX_QUEUE_SIZE) {
			error("function ", _slot, ": no tag left");
			Packet bad = p;
			bad.status = Packet::FLUSHED;
			_tx.sink()->acknowledge_packet(bad);
			return false;
		}

		_taken[tag]      = p;
		_taken_used[tag] = true;
		_taken_count++;

		out.tag      = tag;
		out.endpoint = p.endpoint;
		out.buf      = p.size() ? _dma_base + p.offset() : nullptr;
		out.len      = (unsigned)p.size();
		out.zlp      = p.zlp;
		out.stall    = p.stall;
		return true;
	}

	void ack(unsigned long tag, gadget_status status, unsigned actual)
	{
		if (tag >= TX_QUEUE_SIZE || !_taken_used[tag]) {
			error("function ", _slot, ": ack of unknown packet ", tag);
			return;
		}
		Packet p = _taken[tag];
		_taken_used[tag] = false;
		_taken_count--;

		p.status = Packet::Status(status);
		p.actual = actual;
		_tx.sink()->acknowledge_packet(p);
	}


	/***********************
	 ** Session interface **
	 ***********************/

	bool function(Blob const &fs, Blob const &hs, Blob const &ss,
	              Blob const &strings) override
	{
		Blob const *in[3] = { &fs, &hs, &ss };

		for (unsigned i = 0; i < 3; i++) {
			Usb_gadget::Blob const b { (unsigned char const *)in[i]->base(), in[i]->size() };
			if (!Usb_gadget::parse(b).valid) {
				error("function ", _slot, ": broken descriptors (speed ", i, ")");
				return false;
			}
		}

		for (unsigned i = 0; i < 3; i++) {
			_blob[i].len = (unsigned)in[i]->size();
			memcpy(_blob[i].data, in[i]->base(), _blob[i].len);
		}

		/* NUL-separated strings, index 1 first */
		size_t const n = min(strings.size(), sizeof(_strings) - 1);
		memcpy(_strings, strings.base(), n);
		_strings[n] = 0;
		_string_count = 0;
		for (size_t o = 0; o < n && _string_count < GADGET_MAX_STRINGS; ) {
			_string[_string_count++] = _strings + o;
			o += strlen(_strings + o) + 1;
		}

		_described = true;
		log("function ", _slot, " described: ", _string_count, " strings");

		/* let the Linux side compose and connect */
		Signal_transmitter(_driver_sigh).submit();
		return true;
	}

	Usb_gadget::Event event() override
	{
		if (!_ev_count) return Usb_gadget::Event { };
		Usb_gadget::Event const e = _events[_ev_head];
		_ev_head = (_ev_head + 1) % MAX_EVENTS;
		_ev_count--;
		return e;
	}

	void event_sigh(Signal_context_capability sigh) override
	{
		_event_sigh = sigh;
		if (_ev_count && sigh.valid())
			Signal_transmitter(sigh).submit();
	}

	Capability<Tx> tx_cap() override { return _tx.cap(); }
};


struct Usb_gadget_driver::Root : Root_component<Session_component>
{
	Env &_env;

	Signal_context_capability const _driver_sigh;

	struct Slot
	{
		Label                    label   { };
		Session_component       *session = nullptr;
		genode_shared_dataspace *parked  = nullptr;  /* buffer of a closed session */
	};

	Slot     _slots[GADGET_MAX_SLOTS] { };
	unsigned _slot_count = 0;

	struct gadget_identity _identity { };

	Text _manufacturer { }, _product { }, _serial { };

	Create_result _create_session(char const *args) override
	{
		Session_label const label = label_from_args(args);
		Label const name { label.last_element() };

		size_t const tx_buf_size = Arg_string::find_arg(args, "tx_buf_size").ulong_value(0);
		size_t const ram_quota   = Arg_string::find_arg(args, "ram_quota").ulong_value(0);

		if (tx_buf_size > ram_quota) {
			error("session \"", label, "\": tx buffer exceeds the RAM quota");
			throw Insufficient_ram_quota();
		}

		for (unsigned i = 0; i < _slot_count; i++) {
			Slot &s = _slots[i];
			if (s.label != name) continue;

			if (s.session) {
				error("function \"", name, "\" is in use already");
				throw Service_denied();
			}
			s.session = new (md_alloc())
				Session_component(_env, i, tx_buf_size, _driver_sigh);
			log("function \"", name, "\" connected (slot ", i, ")");
			return *s.session;
		}

		error("no <function> for session \"", label, "\"");
		throw Service_denied();
	}

	void _destroy_session(Session_component &session) override
	{
		Slot &s = _slots[session.slot()];
		s.session = nullptr;
		log("function \"", s.label, "\" disconnected");

		/*
		 * The buffer is freed once the Linux side has unbound the gadget. A
		 * session that came while an older buffer was parked was hidden
		 * from the Linux side ('session()'), its buffer can go now.
		 */
		if (s.parked)
			lx_emul_shared_dma_buffer_free(session.ds());
		else
			s.parked = session.ds();

		Genode::destroy(md_alloc(), &session);
		Signal_transmitter(_driver_sigh).submit();
	}

	Root(Env &env, Allocator &md_alloc, Node const &config,
	     Signal_context_capability driver_sigh)
	:
		Root_component<Session_component>(&env.ep().rpc_ep(), &md_alloc),
		_env(env), _driver_sigh(driver_sigh)
	{
		config.for_each_sub_node("function", [&] (Node const &f) {
			if (_slot_count < GADGET_MAX_SLOTS)
				_slots[_slot_count++].label = f.attribute_value("label", Label());
		});

		_manufacturer = config.attribute_value("manufacturer", Text("Genode"));
		_product      = config.attribute_value("product",      Text("USB gadget"));
		_serial       = config.attribute_value("serial",       Text("0001"));

		_identity.vendor_id    = (unsigned short)config.attribute_value("vendor_id",  0x1d6bu);
		_identity.product_id   = (unsigned short)config.attribute_value("product_id", 0x0104u);
		_identity.max_power_ma = config.attribute_value("max_power_ma", 100u);
		_identity.manufacturer = _manufacturer.string();
		_identity.product      = _product.string();
		_identity.serial       = _serial.string();

		log("gadget ", Hex(_identity.vendor_id), ":", Hex(_identity.product_id),
		    " \"", _manufacturer, "\" \"", _product, "\", ", _slot_count, " function(s)");
	}

	/**
	 * Session of 'slot' as the Linux side sees it: none while the buffer
	 * of a closed one is still parked (requests may point into it)
	 */
	Session_component *session(unsigned slot)
	{
		if (slot >= _slot_count || _slots[slot].parked) return nullptr;
		return _slots[slot].session;
	}

	void released(unsigned slot)
	{
		if (slot >= _slot_count || !_slots[slot].parked) return;
		lx_emul_shared_dma_buffer_free(_slots[slot].parked);
		_slots[slot].parked = nullptr;

		/* a session that waited behind the parked buffer is visible now */
		if (_slots[slot].session)
			Signal_transmitter(_driver_sigh).submit();
	}
};


static Usb_gadget_driver::Root *_root;


void usb_gadget_sessions_init(Genode::Env &env, Genode::Allocator &alloc,
                              Genode::Node const &config,
                              Genode::Signal_context_capability sigh)
{
	static Usb_gadget_driver::Root root(env, alloc, config, sigh);
	_root = &root;
	env.parent().announce(env.ep().manage(root));
}


extern "C" void gadget_identity(struct gadget_identity *out) { *out = _root->_identity; }


extern "C" unsigned gadget_slot_count(void) { return _root->_slot_count; }


extern "C" int gadget_slot_ready(unsigned slot)
{
	auto *s = _root->session(slot);
	return s && s->described();
}


extern "C" unsigned gadget_string_count(unsigned slot)
{
	auto *s = _root->session(slot);
	return s ? s->string_count() : 0;
}


extern "C" char const *gadget_string(unsigned slot, unsigned i)
{
	auto *s = _root->session(slot);
	return s ? s->string(i) : "";
}


extern "C" unsigned char const *gadget_descriptors(unsigned slot, enum gadget_speed speed,
                                                   unsigned *len)
{
	auto *s = _root->session(slot);
	if (!s) { *len = 0; return nullptr; }
	*len = s->blob(speed).len;
	return s->blob(speed).data;
}


extern "C" unsigned gadget_compose(unsigned slot, enum gadget_speed speed,
                                   unsigned char interface_base, unsigned char string_base,
                                   unsigned char const *ep_address, unsigned ep_count,
                                   unsigned char *out)
{
	auto *s = _root->session(slot);
	if (!s) return 0;

	Usb_gadget::Blob const in { s->blob(speed).data, s->blob(speed).len };
	Usb_gadget::Slot_map const map { interface_base, string_base, ep_address, ep_count };

	return (unsigned)Usb_gadget::compose_function(in, map, out, GADGET_BLOB_MAX);
}


extern "C" int gadget_next_packet(unsigned slot, struct gadget_packet *out)
{
	auto *s = _root->session(slot);
	return s && s->next_packet(*out);
}


extern "C" void gadget_ack(unsigned slot, unsigned long tag, enum gadget_status status,
                           unsigned actual)
{
	auto *s = _root->session(slot);
	if (s) s->ack(tag, status, actual);
}


extern "C" void gadget_event(unsigned slot, enum gadget_event_type type,
                             enum gadget_speed speed, unsigned char const *setup)
{
	auto *s = _root->session(slot);
	if (!s) return;

	Usb_gadget::Event e { };
	e.type  = Usb_gadget::Event::Type(type);
	e.speed = speed == GADGET_SS ? Usb_gadget::Event::SUPER
	        : speed == GADGET_HS ? Usb_gadget::Event::HIGH
	                             : Usb_gadget::Event::FULL;
	if (setup)
		for (unsigned i = 0; i < 8; i++) e.setup[i] = setup[i];
	s->push(e);
}


extern "C" void gadget_slot_released(unsigned slot) { _root->released(slot); }
