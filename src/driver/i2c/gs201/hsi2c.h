/*
 * \brief  Samsung HSI2C master (USI v2), register-level logic
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * Everything the driver does with the controller, without any Genode
 * dependency: the register map, the timing calculation, the session
 * rules and the polled transfer. The registers and the clock are reached
 * through the template argument 'IO':
 *
 *   Word read(unsigned offset);              (32-bit register)
 *   void write(unsigned offset, Word value);
 *   Wide now_us();                           (64-bit time)
 *   void delay_us(Wide us);
 *
 * The component (main.cc) backs 'IO' with the device's MMIO window and a
 * timer session, the host tests (test/) with a model of the controller.
 *
 * Reference: google-modules/soc/gs drivers/i2c/busses/i2c-exynos5.c
 * (auto mode, the polling paths of exynos5_i2c_xfer_msg).
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _HSI2C_H_
#define _HSI2C_H_

namespace Hsi2c {

	/* own names: the header is used next to Genode's and the host's types */
	using Byte = unsigned char;        /*  8 bit */
	using Word = unsigned int;         /* 32 bit */
	using Wide = unsigned long long;   /* 64 bit */

	/* register map (i2c-exynos5.c) */
	enum Reg : unsigned {
		CTL          = 0x00,
		FIFO_CTL     = 0x04,
		TRAILING_CTL = 0x08,
		INT_ENABLE   = 0x20,
		INT_STATUS   = 0x24,
		FIFO_STATUS  = 0x30,
		TX_DATA      = 0x34,
		RX_DATA      = 0x38,
		CONF         = 0x40,
		AUTO_CONF    = 0x44,
		TIMEOUT      = 0x48,
		TRANS_STATUS = 0x50,
		TIMING_FS1   = 0x60,
		TIMING_FS2   = 0x64,
		TIMING_FS3   = 0x68,
		ADDR         = 0x70,
		USI_CON      = 0xc4,
	};

	enum : Word {

		/* CTL */
		CTL_MASTER = 1u << 3,
		CTL_RXCHON = 1u << 6,
		CTL_TXCHON = 1u << 7,
		CTL_SW_RST = 1u << 31,

		/* FIFO_CTL */
		FIFO_RX_EN = 1u << 0,
		FIFO_TX_EN = 1u << 1,

		/* INT_ENABLE, INT_STATUS */
		INT_TRANSFER_DONE = 1u << 7,
		INT_NODEV         = 1u << 10,
		INT_ERROR_MASK    = 0xfu << 8,   /* HSI2C_INT_CHK_TRANS_STATE */

		/* FIFO_STATUS */
		FIFO_TX_LVL_MASK = 0x7f,
		FIFO_TX_EMPTY    = 1u << 8,
		FIFO_RX_EMPTY    = 1u << 24,

		/* CONF */
		CONF_AUTO_MODE = 1u << 31,

		/* AUTO_CONF */
		AUTO_LEN_MASK   = 0xffff,
		AUTO_READ       = 1u << 16,
		AUTO_STOP       = 1u << 17,
		AUTO_MASTER_RUN = 1u << 31,

		/* TIMEOUT */
		TIMEOUT_EN = 1u << 31,

		TRAILING_COUNT = 0xffffff,
	};

	enum { FIFO_SIZE = 16, FIFO_TRIGGER_MAX = 8, MAX_MESSAGE_BYTES = 8 };

	struct Timing;
	struct Policy;

	enum class Result { OK, NO_ACK, BUS_ERROR, TIMEOUT, INVALID };

	static inline char const *name(Result r)
	{
		switch (r) {
		case Result::OK:        return "ok";
		case Result::NO_ACK:    return "no ACK";
		case Result::BUS_ERROR: return "bus error";
		case Result::TIMEOUT:   return "timeout";
		case Result::INVALID:   return "invalid message";
		}
		return "?";
	}

	template <typename IO> class Master;

	static inline Word   fifo_ctl(unsigned len);
	static inline Word   addr_reg(Word prev, Byte address);
	static inline Word   ctl_reg(Word prev, bool read);
	static inline Word   auto_conf_reg(Word prev, unsigned len,
	                                   bool read, bool stop);
	static inline Wide   message_timeout_us(unsigned len, Word bus_hz);
	static inline Result classify(Word int_status);
}


/**
 * Timing registers for standard mode (up to 100 kHz) and fast mode (up to
 * 400 kHz), calculated as exynos5_i2c_set_timing does
 *
 * The values are the fields only: 'div' goes to TIMING_FS3[23:16],
 * 'scl_high' to TIMING_FS2[7:0] and 'start_hold' to TIMING_FS1[23:16], the
 * other bits of the registers keep their reset values.
 */
struct Hsi2c::Timing
{
	enum { STANDARD_HZ = 100000, FAST_HZ = 400000 };

	bool valid      = false;
	Word div        = 0;
	Word scl_high   = 0;
	Word start_hold = 0;
	Word scl_hz     = 0;   /* resulting bus clock */

	/* 'n' zero bits from the LSB of a byte */
	static Word _mask(Word n) { return n >= 8 ? 0 : (0xffu << n) & 0xff; }

	Timing(Word ip_clock_hz, Word bus_hz)
	{
		if (!ip_clock_hz || !bus_hz || bus_hz > FAST_HZ)
			return;

		Wide const raw_div = (Wide)ip_clock_hz / ((Wide)bus_hz * 16);

		/* the divider field has 8 bits, the driver does not wrap it */
		if (raw_div > 0xff)
			return;

		div = (Word)raw_div;

		Wide const mhz_x = (bus_hz <= STANDARD_HZ)
		                 ? 25 * ((Wide)ip_clock_hz / 1000000)
		                 : (9 * (Wide)ip_clock_hz) / 1000000;
		Word const high  = (Word)(mhz_x / ((div + 1) * 10));

		scl_high   = _mask(high);
		start_hold = _mask(high ? high - 1 : 0);
		scl_hz     = ip_clock_hz / ((div + 1) * 16);
		valid      = true;
	}

	Word fs1(Word prev) const { return (prev & ~0x00ff0000u) | (start_hold << 16); }
	Word fs2(Word prev) const { return (prev & ~0x000000ffu) |  scl_high; }
	Word fs3(Word prev) const { return (prev & ~0x00ff0000u) | (div << 16); }
};


/**
 * What a session may do: one slave address, taken from an allow list, and
 * a limit for the length of a write message
 *
 * The bus of the gauge also carries the charger. A session is usable only
 * if its address is in the driver's explicit list, and a session of a
 * read-only client is limited to write messages of one byte (the register
 * pointer).
 */
struct Hsi2c::Policy
{
	Byte     address   = 0;
	bool     permitted = false;
	unsigned max_write = MAX_MESSAGE_BYTES;

	static bool address_valid(unsigned long a) { return a >= 0x08 && a <= 0x77; }

	static bool listed(unsigned long address, unsigned long const *list, unsigned count)
	{
		if (!address_valid(address))
			return false;

		for (unsigned i = 0; i < count; i++)
			if (list[i] == address)
				return true;

		return false;
	}

	Policy() { }

	Policy(unsigned long address, unsigned long const *list, unsigned count,
	       unsigned max_write)
	:
		address((Byte)(address & 0x7f)),
		permitted(listed(address, list, count)),
		max_write(max_write > MAX_MESSAGE_BYTES ? (unsigned)MAX_MESSAGE_BYTES
		                                        : max_write)
	{ }

	bool message_ok(bool read, unsigned len) const
	{
		if (!permitted || len == 0 || len > MAX_MESSAGE_BYTES)
			return false;

		return read || len <= max_write;
	}
};


/*
 * FIFO control: both FIFOs on, trigger levels at the message length, at
 * most 8
 */
Hsi2c::Word Hsi2c::fifo_ctl(unsigned len)
{
	unsigned const level = len > FIFO_TRIGGER_MAX ? (unsigned)FIFO_TRIGGER_MAX : len;

	return FIFO_RX_EN | FIFO_TX_EN | (level << 4) | (level << 16);
}


/*
 * Address register: slave address in the master field [19:10], the slave
 * field [9:0] cleared, master id [31:24] = 7 (not high-speed mode)
 */
Hsi2c::Word Hsi2c::addr_reg(Word prev, Byte address)
{
	prev &= ~((0x3ffu << 10) | 0x3ffu | (0xffu << 24));
	return prev | (0x7u << 24) | ((Word)(address & 0x7f) << 10);
}


Hsi2c::Word Hsi2c::ctl_reg(Word prev, bool read)
{
	prev &= ~(CTL_RXCHON | CTL_TXCHON);
	return prev | (read ? CTL_RXCHON : CTL_TXCHON);
}


/*
 * Auto-mode configuration without MASTER_RUN: length, direction and
 * whether the message ends with a stop condition (without it the next
 * message begins with a repeated start)
 */
Hsi2c::Word Hsi2c::auto_conf_reg(Word prev, unsigned len, bool read, bool stop)
{
	prev &= ~(AUTO_LEN_MASK | AUTO_READ | AUTO_STOP | AUTO_MASTER_RUN);
	return prev | (len & AUTO_LEN_MASK) | (read ? AUTO_READ : 0u)
	            | (stop ? AUTO_STOP : 0u);
}


/* (bytes * 9 bits / bus clock) * 3, at least 100 ms, as the reference */
Hsi2c::Wide Hsi2c::message_timeout_us(unsigned len, Word bus_hz)
{
	Wide const us = bus_hz ? ((Wide)len * 9 * 1000000 / bus_hz) * 3 : 0;
	return us < 100000 ? 100000 : us;
}


/* error bits of the interrupt status */
Hsi2c::Result Hsi2c::classify(Word int_status)
{
	if (int_status & INT_NODEV)      return Result::NO_ACK;
	if (int_status & INT_ERROR_MASK) return Result::BUS_ERROR;
	return Result::OK;
}


template <typename IO>
class Hsi2c::Master
{
	public:

		/* what the controller looked like when a message failed */
		struct Failure
		{
			Result   result       = Result::OK;
			bool     read         = false;
			unsigned done         = 0;   /* bytes moved before the failure */
			Word     int_status   = 0;
			Word     fifo_status  = 0;
			Word     trans_status = 0;
			Word     ctl          = 0;
			Word     conf         = 0;
			Word     auto_conf    = 0;
		};

		/* registers as found before 'init' touched them */
		struct Previous
		{
			Word usi_con = 0, ctl = 0, conf = 0, fs1 = 0, fs2 = 0, fs3 = 0,
			     addr = 0, int_enable = 0, int_status = 0, trans_status = 0;
		};

		enum { POLL_US = 50, DONE_WAIT_US = 5000 };

	private:

		IO      &_io;
		Timing   _timing;
		Word     _bus_hz;
		Failure  _failure { };

		/* messages whose TRANSFER_DONE did not show up after the last byte */
		unsigned _late_done = 0;

		void _clear_pending()
		{
			_io.write(INT_STATUS, _io.read(INT_STATUS));
		}

		void _quiesce()
		{
			_io.write(INT_ENABLE, 0);
			_clear_pending();
		}

		Result _fail(Result result, bool read, unsigned done, Word int_status)
		{
			_failure = { .result       = result,
			             .read         = read,
			             .done         = done,
			             .int_status   = int_status,
			             .fifo_status  = _io.read(FIFO_STATUS),
			             .trans_status = _io.read(TRANS_STATUS),
			             .ctl          = _io.read(CTL),
			             .conf         = _io.read(CONF),
			             .auto_conf    = _io.read(AUTO_CONF) };
			_quiesce();
			reset();
			return result;
		}

		void _start(Byte address, bool read, unsigned len, bool stop)
		{
			_io.write(TIMEOUT, _io.read(TIMEOUT) & ~TIMEOUT_EN);
			_io.write(TRAILING_CTL, TRAILING_COUNT);
			_io.write(FIFO_CTL, fifo_ctl(len));

			_clear_pending();

			_io.write(ADDR, addr_reg(_io.read(ADDR), address));
			_io.write(CTL,  ctl_reg(_io.read(CTL), read));

			/*
			 * The status bits are polled. They are enabled so that they
			 * are latched (the reference's interrupt mode enables the
			 * same set); the interrupt itself stays masked at the
			 * interrupt controller, nobody requests it.
			 */
			_io.write(INT_ENABLE, INT_TRANSFER_DONE | INT_ERROR_MASK);

			Word const conf = auto_conf_reg(_io.read(AUTO_CONF), len, read, stop);
			_io.write(AUTO_CONF, conf);
			_io.write(AUTO_CONF, conf | AUTO_MASTER_RUN);
		}

		/* wait for TRANSFER_DONE after the last byte of a read message */
		void _await_done()
		{
			Wide const deadline = _io.now_us() + DONE_WAIT_US;

			for (;;) {
				if (_io.read(INT_STATUS) & INT_TRANSFER_DONE)
					return;
				if (_io.now_us() >= deadline)
					break;
				_io.delay_us(POLL_US);
			}
			_late_done++;
		}

	public:

		Master(IO &io, Word ip_clock_hz, Word bus_hz)
		:
			_io(io), _timing(ip_clock_hz, bus_hz), _bus_hz(bus_hz)
		{ }

		bool            timing_valid() const { return _timing.valid; }
		Timing  const & timing()       const { return _timing; }
		Failure const & failure()      const { return _failure; }
		unsigned        late_done()    const { return _late_done; }

		Previous previous() const
		{
			return { .usi_con      = _io.read(USI_CON),
			         .ctl          = _io.read(CTL),
			         .conf         = _io.read(CONF),
			         .fs1          = _io.read(TIMING_FS1),
			         .fs2          = _io.read(TIMING_FS2),
			         .fs3          = _io.read(TIMING_FS3),
			         .addr         = _io.read(ADDR),
			         .int_enable   = _io.read(INT_ENABLE),
			         .int_status   = _io.read(INT_STATUS),
			         .trans_status = _io.read(TRANS_STATUS) };
		}

		/**
		 * Software reset, timing, master and auto mode (exynos5_i2c_reset)
		 */
		void reset()
		{
			_io.write(CTL, _io.read(CTL) |  CTL_SW_RST);
			_io.write(CTL, _io.read(CTL) & ~CTL_SW_RST);

			_io.write(TIMING_FS3, _timing.fs3(_io.read(TIMING_FS3)));
			_io.write(TIMING_FS2, _timing.fs2(_io.read(TIMING_FS2)));
			_io.write(TIMING_FS1, _timing.fs1(_io.read(TIMING_FS1)));

			_io.write(CTL,  CTL_MASTER);
			_io.write(CONF, _io.read(CONF) | CONF_AUTO_MODE);
		}

		/**
		 * Bring the controller up: release the USI reset, drop what is
		 * pending, reset and configure (exynos5_i2c_probe)
		 *
		 * \return false if the timing cannot be calculated; the controller
		 *         is not touched then
		 */
		bool init()
		{
			if (!_timing.valid)
				return false;

			_io.write(USI_CON, 0);
			_io.write(INT_ENABLE, 0);
			_clear_pending();
			reset();
			return true;
		}

		/**
		 * One message in auto mode, polled
		 *
		 * \param stop  end with a stop condition; false: the next message
		 *              starts with a repeated start
		 */
		Result message(Byte address, bool read, Byte *bytes, unsigned len,
		               bool stop)
		{
			if (!_timing.valid || !bytes || len == 0 || len > MAX_MESSAGE_BYTES)
				return Result::INVALID;

			_start(address, read, len, stop);

			Wide const deadline = _io.now_us() + message_timeout_us(len, _bus_hz);

			unsigned done = 0;

			for (;;) {

				Word const status = _io.read(INT_STATUS);

				Result const error = classify(status);
				if (error != Result::OK)
					return _fail(error, read, done, status);

				if (read) {
					while (done < len && !(_io.read(FIFO_STATUS) & FIFO_RX_EMPTY))
						bytes[done++] = (Byte)_io.read(RX_DATA);

					if (done == len) {
						_await_done();
						break;
					}
				} else {
					while (done < len
					    && (_io.read(FIFO_STATUS) & FIFO_TX_LVL_MASK) < FIFO_SIZE)
						_io.write(TX_DATA, bytes[done++]);

					if (done == len && (status & INT_TRANSFER_DONE)
					 && (_io.read(FIFO_STATUS) & FIFO_TX_EMPTY))
						break;
				}

				if (_io.now_us() >= deadline)
					return _fail(Result::TIMEOUT, read, done, status);

				_io.delay_us(POLL_US);
			}

			/* an error may be flagged together with the end of the message */
			Word const status = _io.read(INT_STATUS);
			Result const error = classify(status);
			if (error != Result::OK)
				return _fail(error, read, done, status);

			_quiesce();
			return Result::OK;
		}
};

#endif /* _HSI2C_H_ */
