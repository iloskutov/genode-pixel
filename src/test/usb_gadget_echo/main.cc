/*
 * \brief  Test of the USB gadget driver: a CDC ACM function that echoes
 * \author Ivan Loskutov
 * \date   2026-10-28
 *
 * The host sees a serial port (Linux: /dev/ttyACM*). Every byte written to
 * it comes back unchanged. Endpoints (usb_gadget/acm.h): 1 notification
 * (not used), 2 bulk IN, 3 bulk OUT. OUT buffers have the size of one max
 * packet of the enabled speed: a transfer of exactly one max packet ends
 * without a zero-length packet, which would not complete a larger buffer.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/allocator_avl.h>
#include <base/component.h>
#include <base/heap.h>
#include <usb_gadget/acm.h>
#include <usb_gadget_session/connection.h>

namespace Test {

	using namespace Genode;

	struct Main;

	using Packet = Usb_gadget::Packet_descriptor;
	using Event  = Usb_gadget::Event;
	using Blob   = Usb_gadget::Session::Blob;
}


struct Test::Main
{
	enum { EP_IN = 2, EP_OUT = 3, OUT_PACKETS = 16 };

	Env &_env;

	Heap          _heap     { _env.ram(), _env.rm() };
	Allocator_avl _tx_alloc { &_heap };

	Usb_gadget::Connection _gadget { _env, _tx_alloc, "echo" };

	Signal_handler<Main> _handler { _env.ep(), *this, &Main::_handle };

	Usb_gadget::Acm _acm { };

	bool     _enabled    = false;
	unsigned _max_packet = 64;

	/* received data not yet echoed, in arrival order */
	Packet   _pending[OUT_PACKETS] { };
	unsigned _pending_head = 0, _pending_count = 0;

	/* class request with a data stage from the host */
	Usb_gadget::Setup _setup { };
	bool              _setup_out = false;

	unsigned long _echoed = 0;

	auto &_source() { return *_gadget.tx(); }

	bool _submit(size_t size, uint8_t ep, auto const &fill_fn)
	{
		if (!_source().ready_to_submit())
			return false;

		Packet p = _gadget.alloc_packet(size, ep);
		if (size && !p.size())
			return false;

		fill_fn(p);
		_source().submit_packet(p);
		return true;
	}

	void _submit_out()
	{
		if (!_submit(_max_packet, EP_OUT, [] (Packet &) { }))
			warning("no room for an OUT buffer");
	}

	/* echo the received data, oldest first, as long as there is room */
	void _echo_pending()
	{
		while (_pending_count) {
			Packet const out = _pending[_pending_head];

			bool const ok = _submit(out.actual, EP_IN, [&] (Packet &in) {
				memcpy(_source().packet_content(in),
				       _source().packet_content(out), out.actual);
				in.zlp = true; });

			if (!ok)
				return;

			_echoed += out.actual;
			_source().release_packet(out);
			_pending_head = (_pending_head + 1) % OUT_PACKETS;
			_pending_count--;

			if (_enabled)
				_submit_out();
		}
	}

	void _answer_setup(unsigned char const *out_data)
	{
		unsigned char in[8] { };
		unsigned long in_len = 0;

		switch (Usb_gadget::handle(_acm, _setup, out_data, in, in_len)) {

		case Usb_gadget::Reply::DATA_IN:
			_submit(in_len, 0, [&] (Packet &p) {
				memcpy(_source().packet_content(p), in, in_len); });
			return;

		case Usb_gadget::Reply::DATA_OUT:
			_setup_out = true;
			_submit(_setup.length, 0, [] (Packet &) { });
			return;

		case Usb_gadget::Reply::STATUS:
			_submit(0, 0, [] (Packet &) { });
			return;

		case Usb_gadget::Reply::STALL:
			_submit(0, 0, [] (Packet &p) { p.stall = true; });
			return;
		}
	}

	void _handle_ack(Packet const &p)
	{
		/* data stage of a class request */
		if (p.endpoint == 0 && _setup_out && p.status == Packet::OK && p.actual) {
			_setup_out = false;
			unsigned char data[8] { };
			memcpy(data, _source().packet_content(p), min((size_t)p.actual, sizeof(data)));
			_source().release_packet(p);
			_answer_setup(data);
			return;
		}

		if (p.endpoint == EP_OUT && p.status == Packet::OK && _enabled) {
			if (!p.actual) {
				/* a zero-length transfer, nothing to echo */
				_source().release_packet(p);
				_submit_out();
				return;
			}
			_pending[(_pending_head + _pending_count++) % OUT_PACKETS] = p;
			return;
		}

		_source().release_packet(p);
	}

	void _handle_event(Event const &e)
	{
		switch (e.type) {

		case Event::ENABLED:
			_max_packet = e.speed == Event::SUPER ? 1024
			            : e.speed == Event::HIGH  ? 512 : 64;
			_enabled = true;
			log("enabled, max packet ", _max_packet);
			for (unsigned i = 0; i < OUT_PACKETS; i++)
				_submit_out();
			return;

		case Event::DISABLED:
			_enabled = false;
			for (; _pending_count; _pending_count--) {
				_source().release_packet(_pending[_pending_head]);
				_pending_head = (_pending_head + 1) % OUT_PACKETS;
			}
			log("disabled, ", _echoed, " bytes echoed");
			return;

		case Event::SETUP:
			_setup = Usb_gadget::Setup {
				e.setup[0], e.setup[1],
				uint16_t(e.setup[2] | e.setup[3] << 8),
				uint16_t(e.setup[4] | e.setup[5] << 8),
				uint16_t(e.setup[6] | e.setup[7] << 8) };
			_setup_out = false;
			_answer_setup(nullptr);
			return;

		default:
			return;
		}
	}

	void _handle()
	{
		/* acknowledgements queued before an event come first */
		while (_source().ack_avail())
			_handle_ack(_source().get_acked_packet());

		for (Event e = _gadget.event(); e.type != Event::NONE; e = _gadget.event())
			_handle_event(e);

		_echo_pending();
	}

	static Blob _blob(auto const &d) { return Blob((char const *)&d, sizeof(d)); }

	Main(Env &env) : _env(env)
	{
		_gadget.event_sigh(_handler);
		_gadget.tx_channel()->sigh_ack_avail(_handler);
		_gadget.tx_channel()->sigh_ready_to_submit(_handler);

		Blob const strings(Usb_gadget::acm_strings, sizeof(Usb_gadget::acm_strings));

		if (!_gadget.function(_blob(Usb_gadget::acm_fs), _blob(Usb_gadget::acm_hs),
		                      _blob(Usb_gadget::acm_ss), strings))
			error("descriptors rejected");
	}
};


void Component::construct(Genode::Env &env) { static Test::Main main(env); }
