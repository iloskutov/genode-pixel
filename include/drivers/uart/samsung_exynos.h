/*
 * \brief  Driver for the Samsung Exynos UART
 * \author Ivan Loskutov
 * \date   2026-02-06
 *
 * The register layout is taken from the Linux driver
 * drivers/tty/serial/samsung_tty.c.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__DRIVERS__UART__SAMSUNG_EXYNOS_H_
#define _INCLUDE__DRIVERS__UART__SAMSUNG_EXYNOS_H_

/* Genode includes */
#include <util/mmio.h>

namespace Genode { class Samsung_exynos_uart; }


/*
 * The driver uses the UART as configured by the boot loader: the baud rate
 * is kept, and characters are written via the transmit FIFO if it is
 * enabled.
 */
class Genode::Samsung_exynos_uart : Mmio<0x28>
{
	private:

		/**
		 * FIFO control register
		 */
		struct Ufcon : Register<0x08, 32>
		{
			struct Fifo_enable : Bitfield<0,1> { };
		};

		/**
		 * Transmit/receive status register
		 */
		struct Utrstat : Register<0x10, 32>
		{
			struct Rx_buf_data_ready : Bitfield<0,1> { };
			struct Tx_buf_empty      : Bitfield<1,1> { };
		};

		/**
		 * FIFO status register
		 */
		struct Ufstat : Register<0x18, 32>
		{
			struct Rx_count : Bitfield<0,8>  { };
			struct Rx_full  : Bitfield<8,1>  { };
			struct Tx_full  : Bitfield<24,1> { };
		};

		/**
		 * Transmit buffer register
		 */
		struct Utxh : Register<0x20, 32>
		{
			struct Data : Bitfield<0,8> { };
		};

		/**
		 * Receive buffer register
		 */
		struct Urxh : Register<0x24, 32>
		{
			struct Data : Bitfield<0,8> { };
		};

		bool _fifo_enabled() { return read<Ufcon::Fifo_enable>(); }

	public:

		Samsung_exynos_uart(addr_t const base, uint32_t, uint32_t)
		: Mmio({(char *)base, Mmio::SIZE}) { }

		void init() { }

		bool char_avail()
		{
			if (!_fifo_enabled())
				return read<Utrstat::Rx_buf_data_ready>();

			Ufstat::access_t const status = read<Ufstat>();

			return Ufstat::Rx_count::get(status) || Ufstat::Rx_full::get(status);
		}

		char get_char() { return (char)read<Urxh::Data>(); }

		void put_char(char const c)
		{
			if (_fifo_enabled())
				while (read<Ufstat::Tx_full>());
			else
				while (read<Utrstat::Tx_buf_empty>() == 0);

			write<Utxh::Data>(c);
		}
};

#endif /* _INCLUDE__DRIVERS__UART__SAMSUNG_EXYNOS_H_ */
