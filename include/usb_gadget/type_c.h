/*
 * \brief  Cable orientation from the Type-C controller's CC status
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * TCPCI CC_STATUS (register 0x1d, linux/usb/tcpci.h): CC1 state in bits
 * 1:0, CC2 state in bits 3:2. The phone is the sink, so the line on which
 * it sees the host's Rp (state != 0) is the connected one; it tells the
 * USBDP PHY which SuperSpeed lane pair to use. No Genode dependencies.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__USB_GADGET__TYPE_C_H_
#define _INCLUDE__USB_GADGET__TYPE_C_H_

namespace Usb_gadget {

	enum class Polarity { CC1, CC2, NONE };

	/**
	 * Connected CC line, NONE if none or both lines see a termination
	 */
	static inline Polarity polarity(unsigned char cc_status)
	{
		bool const cc1 = (cc_status >> 0) & 3;
		bool const cc2 = (cc_status >> 2) & 3;

		if (cc1 == cc2) return Polarity::NONE;
		return cc1 ? Polarity::CC1 : Polarity::CC2;
	}
}

#endif /* _INCLUDE__USB_GADGET__TYPE_C_H_ */
