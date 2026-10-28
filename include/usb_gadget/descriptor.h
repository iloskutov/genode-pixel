/*
 * \brief  Descriptors of gadget functions: check, renumber, compose
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * A function (session client of the gadget driver) describes itself with
 * function-local numbers, as with Linux' functionfs: interfaces from 0,
 * strings from 1, endpoints 1.. in descriptor order (the address in its
 * endpoint descriptors carries the direction only). The driver places the
 * function in the configuration: interface and string numbers get a base,
 * endpoint addresses come from the controller ('usb_ep_autoconfig').
 * Unlike functionfs, the interface numbers inside the CDC union and call
 * management descriptors are renumbered too. A function with more than
 * one interface gets an interface association descriptor if it has none.
 * No Genode dependencies.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__USB_GADGET__DESCRIPTOR_H_
#define _INCLUDE__USB_GADGET__DESCRIPTOR_H_

namespace Usb_gadget {

	enum Descriptor_type : unsigned char {
		DT_CONFIG = 2, DT_INTERFACE = 4, DT_ENDPOINT = 5, DT_IAD = 0x0b,
		DT_CS_INTERFACE = 0x24, DT_SS_EP_COMPANION = 0x30 };

	enum Cdc_subtype : unsigned char { CDC_CALL_MANAGEMENT = 0x01, CDC_UNION = 0x06 };

	enum class Speed { FULL, HIGH, SUPER };

	struct Blob { unsigned char const *ptr; unsigned long len; };

	/*
	 * Standard descriptors as packed structs. The multi-byte fields are
	 * little endian, as USB has them.
	 */
	static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

	enum Endpoint_attributes : unsigned char { EP_BULK = 0x02, EP_INTERRUPT = 0x03 };

	enum : unsigned char { EP_IN = 0x80 };   /* direction bit of an address */

	struct [[gnu::packed]] Interface_descriptor
	{
		unsigned char length = 9, type = DT_INTERFACE;
		unsigned char number, alternate = 0, num_endpoints;
		unsigned char cls, subclass, protocol, string;
	};

	struct [[gnu::packed]] Endpoint_descriptor
	{
		unsigned char  length = 7, type = DT_ENDPOINT;
		unsigned char  address, attributes;
		unsigned short max_packet;
		unsigned char  interval;
	};

	struct [[gnu::packed]] Ss_companion_descriptor
	{
		unsigned char  length = 6, type = DT_SS_EP_COMPANION;
		unsigned char  max_burst = 0, attributes = 0;
		unsigned short bytes_per_interval = 0;
	};

	static_assert(sizeof(Interface_descriptor)    == 9);
	static_assert(sizeof(Endpoint_descriptor)     == 7);
	static_assert(sizeof(Ss_companion_descriptor) == 6);

	/**
	 * Descriptors of a packed struct as blob
	 */
	static inline Blob blob(auto const &descriptors)
	{
		return { (unsigned char const *)&descriptors, sizeof(descriptors) };
	}

	struct Function_info
	{
		unsigned interfaces = 0;
		unsigned endpoints  = 0;
		unsigned strings    = 0;   /* highest string index used */
		bool     iad        = false;
		bool     valid      = false;
	};

	struct Slot_map
	{
		unsigned char        interface_base;
		unsigned char        string_base;
		unsigned char const *ep_address;  /* address of local endpoint i+1 */
		unsigned             ep_count;
	};

	/**
	 * Call 'fn(descriptor, length)' for each descriptor
	 *
	 * \return false if the blob is empty or a length is broken
	 */
	static inline bool for_each_descriptor(Blob b, auto const &fn)
	{
		if (!b.ptr || !b.len) return false;

		for (unsigned long o = 0; o < b.len; ) {
			unsigned char const len = b.ptr[o];
			if (len < 2 || o + len > b.len) return false;
			fn(b.ptr + o, len);
			o += len;
		}
		return true;
	}

	static inline Function_info parse(Blob b)
	{
		Function_info info { };
		bool ok = true, in_interface = false;

		auto const max = [] (unsigned &v, unsigned n) { if (n > v) v = n; };

		bool const walked = for_each_descriptor(b, [&] (unsigned char const *d, unsigned len) {
			switch (d[1]) {
			case DT_INTERFACE:
				if (len < 9) { ok = false; return; }
				if (d[2] > info.interfaces) ok = false;          /* not dense */
				if (d[2] == info.interfaces) info.interfaces++;
				max(info.strings, d[8]);
				in_interface = true;
				return;
			case DT_ENDPOINT:
				if (len < 7 || !in_interface) { ok = false; return; }
				info.endpoints++;
				return;
			case DT_IAD:
				if (len < 8) { ok = false; return; }
				info.iad = true;
				max(info.strings, d[7]);
				return;
			case DT_CS_INTERFACE:
				if (len < 3) ok = false;
				return;
			default:
				return;
			}
		});

		info.valid = walked && ok && info.interfaces;
		return info;
	}

	/**
	 * Copy a function's descriptors into 'out', placed by 'map'
	 *
	 * \return bytes written, 0 if the blob is invalid, 'map' names too few
	 *         endpoints, or 'out' is too small
	 */
	static inline unsigned long compose_function(Blob in, Slot_map const &map,
	                                             unsigned char *out,
	                                             unsigned long out_max)
	{
		Function_info const info = parse(in);
		if (!info.valid || info.endpoints > map.ep_count)
			return 0;

		bool const add_iad = info.interfaces > 1 && !info.iad;
		unsigned long const total = in.len + (add_iad ? 8 : 0);
		if (total > out_max)
			return 0;

		unsigned long o = 0;

		if (add_iad) {
			unsigned char const *first = nullptr;
			for_each_descriptor(in, [&] (unsigned char const *d, unsigned) {
				if (!first && d[1] == DT_INTERFACE) first = d; });

			unsigned char const iad[8] = { 8, DT_IAD, map.interface_base,
			                               (unsigned char)info.interfaces,
			                               first[5], first[6], first[7], 0 };
			for (unsigned i = 0; i < 8; i++) out[o++] = iad[i];
		}

		auto const string = [&] (unsigned char s) {
			return (unsigned char)(s ? s + map.string_base : 0); };

		unsigned ep = 0;
		for_each_descriptor(in, [&] (unsigned char const *s, unsigned len) {
			unsigned char *d = out + o;
			for (unsigned i = 0; i < len; i++) d[i] = s[i];
			o += len;

			switch (d[1]) {
			case DT_INTERFACE:
				d[2] += map.interface_base;
				d[8]  = string(d[8]);
				return;
			case DT_IAD:
				d[2] += map.interface_base;
				d[7]  = string(d[7]);
				return;
			case DT_ENDPOINT:
				d[2] = map.ep_address[ep++];
				return;
			case DT_CS_INTERFACE:
				if (d[2] == CDC_UNION)
					for (unsigned i = 3; i < len; i++) d[i] += map.interface_base;
				if (d[2] == CDC_CALL_MANAGEMENT && len >= 5)
					d[4] += map.interface_base;
				return;
			default:
				return;
			}
		});

		return o;
	}

	/**
	 * Configuration descriptor of the composed functions, in slot order
	 *
	 * \param max_power_ma  bus current, in units of 2 mA (8 mA at
	 *                      SuperSpeed) in the descriptor, rounded up
	 *
	 * \param functions     results of 'compose_function'
	 *
	 * \return bytes written, 0 if a function is broken or 'out' is too small
	 */
	static inline unsigned long compose_config(Blob const *functions, unsigned count,
	                                           Speed speed, unsigned max_power_ma,
	                                           unsigned char *out,
	                                           unsigned long out_max)
	{
		unsigned long total = 9;
		unsigned interfaces = 0;

		/* composed functions: interfaces are numbered already, count alt 0 */
		for (unsigned i = 0; i < count; i++) {
			bool const ok = for_each_descriptor(functions[i],
				[&] (unsigned char const *d, unsigned len) {
					if (d[1] == DT_INTERFACE && len >= 9 && d[3] == 0)
						interfaces++; });
			if (!ok) return 0;
			total += functions[i].len;
		}

		if (total > out_max || total > 0xffff)
			return 0;

		unsigned const unit = (speed == Speed::SUPER) ? 8 : 2;
		unsigned const power = (max_power_ma + unit - 1) / unit;

		unsigned char const header[9] = {
			9, DT_CONFIG, (unsigned char)(total & 0xff), (unsigned char)(total >> 8),
			(unsigned char)interfaces, 1, 0, 0x80,
			(unsigned char)(power > 0xff ? 0xff : power) };

		unsigned long o = 0;
		for (unsigned i = 0; i < 9; i++) out[o++] = header[i];
		for (unsigned f = 0; f < count; f++)
			for (unsigned long i = 0; i < functions[f].len; i++)
				out[o++] = functions[f].ptr[i];

		return o;
	}
}

#endif /* _INCLUDE__USB_GADGET__DESCRIPTOR_H_ */
