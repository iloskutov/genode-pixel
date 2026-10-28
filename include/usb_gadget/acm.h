/*
 * \brief  CDC ACM function: descriptors and class requests
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * The descriptors use function-local numbers (interfaces 0 and 1, string
 * 1, endpoint numbers 1..3 in descriptor order); the gadget driver
 * renumbers them ('compose_function', descriptor.h). Requests from the
 * CDC PSTN subclass spec: SET/GET_LINE_CODING, SET_CONTROL_LINE_STATE,
 * SEND_BREAK. No Genode dependencies.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__USB_GADGET__ACM_H_
#define _INCLUDE__USB_GADGET__ACM_H_

#include <usb_gadget/descriptor.h>

namespace Usb_gadget {

	/*
	 * Class-specific descriptors of the communication interface (CDC 1.10)
	 */

	enum Cdc_acm_subtype : unsigned char { CDC_HEADER = 0x00, CDC_ACM = 0x02 };

	struct [[gnu::packed]] Cdc_header_descriptor
	{
		unsigned char  length = 5, type = DT_CS_INTERFACE, subtype = CDC_HEADER;
		unsigned short cdc_version = 0x0110;
	};

	struct [[gnu::packed]] Cdc_call_management_descriptor
	{
		unsigned char length = 5, type = DT_CS_INTERFACE, subtype = CDC_CALL_MANAGEMENT;
		unsigned char capabilities = 0, data_interface;
	};

	struct [[gnu::packed]] Cdc_acm_descriptor
	{
		unsigned char length = 4, type = DT_CS_INTERFACE, subtype = CDC_ACM;
		unsigned char capabilities;
	};

	struct [[gnu::packed]] Cdc_union_descriptor
	{
		unsigned char length = 5, type = DT_CS_INTERFACE, subtype = CDC_UNION;
		unsigned char control_interface, subordinate_interface;
	};

	/*
	 * Communication interface 0 (string 1) with the header, call management
	 * (data interface 1), ACM (line coding requests), and union (0 -> 1)
	 * descriptors
	 */
	struct [[gnu::packed]] Acm_communication
	{
		Interface_descriptor interface {
			.number = 0, .num_endpoints = 1,
			.cls = 0x02, .subclass = 0x02, .protocol = 0x01, .string = 1 };

		Cdc_header_descriptor          header { };
		Cdc_call_management_descriptor call   { .data_interface = 1 };
		Cdc_acm_descriptor             acm    { .capabilities = 0x02 };
		Cdc_union_descriptor           cdc_union {
			.control_interface = 0, .subordinate_interface = 1 };
	};

	/* data interface 1 with two endpoints */
	static constexpr Interface_descriptor acm_data_interface {
		.number = 1, .num_endpoints = 2,
		.cls = 0x0a, .subclass = 0, .protocol = 0, .string = 0 };

	static constexpr Endpoint_descriptor acm_endpoint(unsigned char  address,
	                                                  unsigned char  attributes,
	                                                  unsigned short max_packet,
	                                                  unsigned char  interval)
	{
		return { .address = address, .attributes = attributes,
		         .max_packet = max_packet, .interval = interval };
	}

	/*
	 * Function-local endpoints: 1 interrupt IN (notification), 2 bulk IN,
	 * 3 bulk OUT
	 */
	enum : unsigned char { ACM_NOTIFY = EP_IN | 1, ACM_BULK_IN = EP_IN | 2,
	                       ACM_BULK_OUT = 3, ACM_NOTIFY_SIZE = 10 };

	/* descriptors at full and high speed */
	struct [[gnu::packed]] Acm_descriptors
	{
		Acm_communication    comm { };
		Endpoint_descriptor  notify;
		Interface_descriptor data = acm_data_interface;
		Endpoint_descriptor  bulk_in, bulk_out;
	};

	static constexpr Acm_descriptors acm_descriptors(unsigned short bulk_max_packet,
	                                                 unsigned char  notify_interval)
	{
		return {
			.notify   = acm_endpoint(ACM_NOTIFY, EP_INTERRUPT, ACM_NOTIFY_SIZE,
			                         notify_interval),
			.bulk_in  = acm_endpoint(ACM_BULK_IN,  EP_BULK, bulk_max_packet, 0),
			.bulk_out = acm_endpoint(ACM_BULK_OUT, EP_BULK, bulk_max_packet, 0) };
	}

	/* full speed: 64-byte bulk packets, notification every 32 frames */
	static constexpr Acm_descriptors acm_fs = acm_descriptors(64, 32);

	/* high speed: 512-byte bulk packets, notification every 2^(9-1) microframes */
	static constexpr Acm_descriptors acm_hs = acm_descriptors(512, 9);

	/* SuperSpeed: 1024-byte bulk packets, each endpoint with its companion */
	struct [[gnu::packed]] Acm_ss_descriptors
	{
		Acm_communication       comm { };
		Endpoint_descriptor     notify = acm_endpoint(ACM_NOTIFY, EP_INTERRUPT,
		                                              ACM_NOTIFY_SIZE, 9);
		Ss_companion_descriptor notify_companion { .bytes_per_interval = ACM_NOTIFY_SIZE };
		Interface_descriptor    data = acm_data_interface;
		Endpoint_descriptor     bulk_in = acm_endpoint(ACM_BULK_IN, EP_BULK, 1024, 0);
		Ss_companion_descriptor bulk_in_companion { };
		Endpoint_descriptor     bulk_out = acm_endpoint(ACM_BULK_OUT, EP_BULK, 1024, 0);
		Ss_companion_descriptor bulk_out_companion { };
	};

	static constexpr Acm_ss_descriptors acm_ss { };

	static_assert(sizeof(Acm_descriptors)    == 58);
	static_assert(sizeof(Acm_ss_descriptors) == 76);

	/* NUL-separated strings, index 1 first */
	static constexpr char acm_strings[] = "CDC ACM";

	struct Setup
	{
		unsigned char  type, request;
		unsigned short value, index, length;
	};

	enum class Reply { DATA_IN, DATA_OUT, STATUS, STALL };

	struct Acm
	{
		unsigned char line_coding[7] = { 0x00, 0xc2, 0x01, 0x00, 0, 0, 8 }; /* 115200 8N1 */
		bool          dtr = false;
	};

	/**
	 * Handle a class request to the ACM function
	 *
	 * \param out_data  data stage of a host-to-device request, nullptr when
	 *                  the setup packet arrives
	 * \param in_buf    receives the data stage of a device-to-host request
	 *                  (at least 7 bytes)
	 * \param in_len    its length, valid for DATA_IN
	 *
	 * \return DATA_OUT: the data stage is needed, call again with it
	 */
	static inline Reply handle(Acm &acm, Setup const &s,
	                           unsigned char const *out_data,
	                           unsigned char *in_buf, unsigned long &in_len)
	{
		enum { OUT = 0x21, IN = 0xa1,
		       SET_LINE_CODING = 0x20, GET_LINE_CODING = 0x21,
		       SET_CONTROL_LINE_STATE = 0x22, SEND_BREAK = 0x23 };

		if (s.type == OUT && s.request == SET_LINE_CODING) {
			if (s.length != sizeof(acm.line_coding)) return Reply::STALL;
			if (!out_data) return Reply::DATA_OUT;
			for (unsigned i = 0; i < sizeof(acm.line_coding); i++)
				acm.line_coding[i] = out_data[i];
			return Reply::STATUS;
		}

		if (s.type == IN && s.request == GET_LINE_CODING) {
			in_len = s.length < sizeof(acm.line_coding) ? s.length
			                                            : sizeof(acm.line_coding);
			for (unsigned i = 0; i < in_len; i++)
				in_buf[i] = acm.line_coding[i];
			return Reply::DATA_IN;
		}

		if (s.type == OUT && s.request == SET_CONTROL_LINE_STATE) {
			acm.dtr = s.value & 1;
			return Reply::STATUS;
		}

		if (s.type == OUT && s.request == SEND_BREAK)
			return Reply::STATUS;

		return Reply::STALL;
	}
}

#endif /* _INCLUDE__USB_GADGET__ACM_H_ */
