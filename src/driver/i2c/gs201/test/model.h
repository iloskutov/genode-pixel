/*
 * \brief  Model of the HSI2C controller and its bus for the host tests
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * Implements the 'IO' interface of Hsi2c::Master. The bus has one slave
 * with 16-bit registers behind an 8-bit register pointer (as the fuel
 * gauge), every other address does not acknowledge. The model moves the
 * bytes of a message as soon as the driver starts it; 'stuck' makes the
 * controller never finish, 'no_done' suppresses TRANSFER_DONE.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _I2C_TEST__MODEL_H_
#define _I2C_TEST__MODEL_H_

#include <cstdint>
#include <cstring>
#include <vector>

#include <hsi2c.h>

struct Model
{
	/* the bus */
	uint8_t  slave_address = 0x36;
	uint16_t slave_reg[256] { };
	uint8_t  pointer = 0;
	bool     pointer_low = true;         /* next byte read is the low byte */

	/* behaviour switches */
	bool stuck   = false;
	bool no_done = false;
	bool lost    = false;                /* flag an error other than NODEV */

	/* controller state */
	uint32_t reg[0x100 / 4] { };
	std::vector<uint8_t> rx_fifo { };
	unsigned tx_pending = 0;             /* bytes the running write still needs */
	bool     running = false;

	/* observations */
	struct Message { uint8_t address; bool read; bool stop; std::vector<uint8_t> bytes; };
	std::vector<Message> bus { };        /* messages that reached the bus */
	std::vector<uint8_t> written { };    /* every byte written to any slave */
	unsigned resets = 0;
	unsigned reads = 0, writes = 0;      /* register accesses */
	uint64_t time_us = 0;
	uint64_t slept_us = 0;

	Model()
	{
		/* reset values of the timing registers (references/dmesg.txt) */
		_reset_registers();
		reg[Hsi2c::USI_CON / 4] = 1;     /* USI_RESET asserted */
	}

	void _reset_registers()
	{
		uint32_t const usi = reg[Hsi2c::USI_CON / 4];
		memset(reg, 0, sizeof(reg));
		reg[Hsi2c::USI_CON    / 4] = usi;
		reg[Hsi2c::TIMING_FS1 / 4] = 0x0100ff00;
		reg[Hsi2c::TIMING_FS2 / 4] = 0x03000300;
		rx_fifo.clear();
		tx_pending = 0;
		running = false;
	}

	uint32_t &_r(unsigned off) { return reg[off / 4]; }

	void _finish()
	{
		running = false;
		if (!no_done)
			_r(Hsi2c::INT_STATUS) |= Hsi2c::INT_TRANSFER_DONE;
	}

	void _run()
	{
		uint32_t const conf = _r(Hsi2c::AUTO_CONF);
		uint8_t  const address = (uint8_t)((_r(Hsi2c::ADDR) >> 10) & 0x7f);
		bool     const read = conf & Hsi2c::AUTO_READ;
		unsigned const len  = conf & Hsi2c::AUTO_LEN_MASK;

		if (_r(Hsi2c::USI_CON) & 1) return;   /* held in reset: nothing happens */
		if (stuck) return;

		bus.push_back({ address, read, (conf & Hsi2c::AUTO_STOP) != 0, { } });
		running = true;

		if (lost) {
			_r(Hsi2c::INT_STATUS) |= 1u << 9;
			running = false;
			return;
		}
		if (address != slave_address) {
			_r(Hsi2c::INT_STATUS) |= Hsi2c::INT_NODEV | Hsi2c::INT_TRANSFER_DONE;
			running = false;
			return;
		}
		if (read) {
			for (unsigned i = 0; i < len; i++) {
				uint16_t const v = slave_reg[pointer];
				uint8_t  const b = pointer_low ? (uint8_t)(v & 0xff) : (uint8_t)(v >> 8);
				if (!pointer_low) pointer++;
				pointer_low = !pointer_low;
				rx_fifo.push_back(b);
				bus.back().bytes.push_back(b);
			}
			_finish();
		} else {
			tx_pending = len;
		}
	}

	/* IO interface */

	uint32_t read(unsigned off)
	{
		reads++;
		time_us++;

		if (off == Hsi2c::RX_DATA) {
			if (rx_fifo.empty()) return 0;
			uint8_t const b = rx_fifo.front();
			rx_fifo.erase(rx_fifo.begin());
			return b;
		}
		if (off == Hsi2c::FIFO_STATUS)
			return (rx_fifo.empty() ? Hsi2c::FIFO_RX_EMPTY : 0u)
			     | ((uint32_t)rx_fifo.size() << 16)
			     | Hsi2c::FIFO_TX_EMPTY;   /* TX bytes leave at once */

		return _r(off);
	}

	void write(unsigned off, uint32_t v)
	{
		writes++;
		time_us++;

		switch (off) {

		case Hsi2c::CTL:
			if ((v & Hsi2c::CTL_SW_RST) && !(_r(off) & Hsi2c::CTL_SW_RST)) {
				resets++;
				_reset_registers();
			}
			_r(off) = v;
			return;

		case Hsi2c::INT_STATUS:
			_r(off) &= ~v;   /* write one to clear */
			return;

		case Hsi2c::AUTO_CONF:
			_r(off) = v;
			if (v & Hsi2c::AUTO_MASTER_RUN)
				_run();
			return;

		case Hsi2c::TX_DATA:
			if (!running || !tx_pending)
				return;
			bus.back().bytes.push_back((uint8_t)v);
			written.push_back((uint8_t)v);
			if (bus.back().bytes.size() == 1) {
				pointer = (uint8_t)v;
				pointer_low = true;
			}
			if (--tx_pending == 0)
				_finish();
			return;

		default:
			_r(off) = v;
		}
	}

	uint64_t now_us() { return ++time_us; }

	void delay_us(uint64_t us) { time_us += us; slept_us += us; }
};

#endif /* _I2C_TEST__MODEL_H_ */
