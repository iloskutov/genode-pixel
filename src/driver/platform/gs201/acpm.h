/*
 * \brief  Access to the PMICs of the GS201 via the ACPM firmware
 * \author Ivan Loskutov
 * \date   2026-02-08
 *
 * The ACPM firmware runs on a Cortex-M core in the ALIVE domain and manages
 * all communication with the PMICs (S2MPG12 and S2MPG13). The application
 * processor accesses PMIC registers via ring buffers in a shared SRAM and a
 * mailbox as doorbell.
 *
 * The IPC protocol is taken from the vendor's Linux driver
 * drivers/soc/google/acpm/ (acpm_ipc.c, acpm_mfd.c), the PMIC registers from
 * include/linux/mfd/samsung/s2mpg12-register.h and s2mpg13-register.h
 * (google-modules/soc/gs).
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _SRC__DRIVER__PLATFORM__GS201__ACPM_H_
#define _SRC__DRIVER__PLATFORM__GS201__ACPM_H_

#include <base/attached_io_mem_dataspace.h>
#include <cpu/mmio_access.h>
#include <power.h>
#include <timer_session/connection.h>

namespace Driver { struct Acpm; }


struct Driver::Acpm : private Noncopyable
{
	Genode::Env &_env;
	Powers      &_powers;

	enum : addr_t {
		/* mailbox between the application processor and the firmware */
		MBOX_BASE = 0x18210000,
		MBOX_SIZE = 0x1000,

		/* SRAM shared with the firmware */
		SRAM_BASE = 0x18500000,
		SRAM_SIZE = 0x48000,
	};

	enum {
		/* mailbox registers */
		MBOX_INTGR1      = 0x0040,  /* doorbell to the firmware */
		MBOX_INTCR0      = 0x0024,  /* interrupt clear */

		/* init data of the firmware within the SRAM */
		ACPM_INITDATA_OFF = 0xA000,

		/* fields of the firmware's framework structure */
		FW_IPC_CHANNELS  = 8,
		FW_IPC_AP_MAX    = 24,

		/* sanity limit for the number of channels found in the SRAM */
		MAX_IPC_CHANNELS = 32,

		/* IPC-channel structure, 72 bytes each */
		IPC_CH_SIZE      = 72,
		IPC_CH_ID        = 0,
		IPC_CH_RX_REAR   = 16,
		IPC_CH_RX_FRONT  = 20,
		IPC_CH_RX_BASE   = 24,
		IPC_CH_TX_REAR   = 40,
		IPC_CH_TX_FRONT  = 44,
		IPC_CH_TX_BASE   = 48,
		IPC_CH_Q_LEN     = 52,
		IPC_CH_Q_ELEM_SZ = 56,

		/* IPC protocol: 6-bit sequence number in the first command word */
		ACPM_SEQ_NUM_SHIFT = 16,
		ACPM_SEQ_NUM_MASK  = 0x3f,
		ACPM_SEQ_NUM_FIRST = 1,
		ACPM_SEQ_NUM_LIMIT = 64,
		ACPM_PMIC_IPC_CH   = 2,
		ACPM_CMD_WORDS     = 4,

		/*
		 * Number of times the receive queue is polled for the response to
		 * a command. It is a bound for the busy waiting, not a time: a
		 * firmware that does not answer must not block the driver forever.
		 */
		MAX_RESPONSE_POLLS = 500000,

		/* PMIC access */
		PMIC_TYPE_PM     = 0x01,
		PMIC_CHANNEL     = 0,       /* S2MPG12, main PMIC */
		PMIC_CHANNEL_SUB = 1,       /* S2MPG13, sub PMIC  */
		ACPM_FUNC_UPDATE = 2,

		/*
		 * Layout of a PMIC command. First word: register in bits 0..7,
		 * PMIC type in bits 8..11, PMIC channel in bits 12..15. Second
		 * word of an update: function in bits 0..7, value in bits 8..15,
		 * mask in bits 16..23.
		 */
		PMIC_TYPE_SHIFT         = 8,
		PMIC_CHANNEL_SHIFT      = 12,
		PMIC_CHANNEL_MASK       = 0xf,
		ACPM_UPDATE_VALUE_SHIFT = 8,
		ACPM_UPDATE_MASK_SHIFT  = 16,

		/* response: status in the topmost byte of the second word, 0 is ok */
		ACPM_STATUS_SHIFT = 24,
		ACPM_STATUS_MASK  = 0xff,
	};

	Attached_io_mem_dataspace _mbox_ds { _env, MBOX_BASE, MBOX_SIZE, false };
	Attached_io_mem_dataspace _sram_ds { _env, SRAM_BASE, SRAM_SIZE, false };

	addr_t const _mbox_base = (addr_t)_mbox_ds.local_addr<void>();
	addr_t const _sram_base = (addr_t)_sram_ds.local_addr<void>();

	struct Ipc_channel
	{
		addr_t   tx_front    = 0;
		addr_t   tx_rear     = 0;
		addr_t   tx_base     = 0;
		addr_t   rx_front    = 0;
		addr_t   rx_rear     = 0;
		addr_t   rx_base     = 0;
		unsigned q_len       = 0;
		unsigned q_elem_size = 0;
		unsigned id          = 0;
		bool     valid       = false;
	};

	Ipc_channel _pmic_ch { };
	uint8_t     _seq = ACPM_SEQ_NUM_FIRST;

	uint32_t _sram_read32(unsigned off)
	{
		return Hw::mmio_read<uint32_t>(_sram_base + off);
	}

	void _mbox_write(unsigned off, uint32_t val)
	{
		Hw::mmio_write<uint32_t>(_mbox_base + off, val);
	}

	void _init_ipc()
	{
		unsigned const channels_off = _sram_read32(ACPM_INITDATA_OFF
		                                           + FW_IPC_CHANNELS);
		unsigned const num_channels = _sram_read32(ACPM_INITDATA_OFF
		                                           + FW_IPC_AP_MAX);

		if (!channels_off || num_channels > MAX_IPC_CHANNELS)
			return;

		for (unsigned i = 0; i < num_channels; i++) {

			unsigned const ch_off = channels_off + i*IPC_CH_SIZE;

			if (_sram_read32(ch_off + IPC_CH_ID) != ACPM_PMIC_IPC_CH)
				continue;

			Ipc_channel &ch = _pmic_ch;

			/* the firmware's receive queue is our transmit queue */
			ch.tx_front    = _sram_base + _sram_read32(ch_off + IPC_CH_RX_FRONT);
			ch.tx_rear     = _sram_base + _sram_read32(ch_off + IPC_CH_RX_REAR);
			ch.tx_base     = _sram_base + _sram_read32(ch_off + IPC_CH_RX_BASE);

			ch.rx_front    = _sram_base + _sram_read32(ch_off + IPC_CH_TX_FRONT);
			ch.rx_rear     = _sram_base + _sram_read32(ch_off + IPC_CH_TX_REAR);
			ch.rx_base     = _sram_base + _sram_read32(ch_off + IPC_CH_TX_BASE);

			ch.q_len       = _sram_read32(ch_off + IPC_CH_Q_LEN);
			ch.q_elem_size = _sram_read32(ch_off + IPC_CH_Q_ELEM_SZ);
			ch.id          = ACPM_PMIC_IPC_CH;
			ch.valid       = true;
			return;
		}

		Genode::error("ACPM firmware provides no PMIC channel");
	}

	/**
	 * Send a command and wait for its response, which replaces 'cmd'
	 */
	bool _send(Ipc_channel const &ch, uint32_t cmd[ACPM_CMD_WORDS])
	{
		if (!ch.valid) return false;

		auto read32  = [] (addr_t va) {
			return Hw::mmio_read<uint32_t>(va); };
		auto write32 = [] (addr_t va, uint32_t v) {
			Hw::mmio_write<uint32_t>(va, v); };

		uint8_t const seq = _seq;
		if (++_seq >= ACPM_SEQ_NUM_LIMIT) _seq = ACPM_SEQ_NUM_FIRST;

		cmd[0] |= ((uint32_t)seq & ACPM_SEQ_NUM_MASK) << ACPM_SEQ_NUM_SHIFT;

		unsigned const tx_front = read32(ch.tx_front);
		unsigned const tx_rear  = read32(ch.tx_rear);
		unsigned const next     = (tx_front + 1) % ch.q_len;

		if (next == tx_rear)
			return false;

		addr_t const slot = ch.tx_base + ch.q_elem_size*tx_front;
		for (unsigned w = 0; w < ACPM_CMD_WORDS; w++)
			write32(slot + w*sizeof(uint32_t), cmd[w]);

		write32(ch.tx_front, next);
		_mbox_write(MBOX_INTGR1, 1u << ch.id);

		for (unsigned poll = 0; poll < MAX_RESPONSE_POLLS; poll++) {

			unsigned const rx_front = read32(ch.rx_front);
			unsigned       rx_rear  = read32(ch.rx_rear);

			if (rx_rear == rx_front)
				continue;

			while (rx_rear != rx_front) {

				addr_t   const rslot   = ch.rx_base + ch.q_elem_size*rx_rear;
				uint32_t const w0      = read32(rslot);
				unsigned const rsp_seq = (w0 >> ACPM_SEQ_NUM_SHIFT)
				                       & ACPM_SEQ_NUM_MASK;

				if (rsp_seq == seq) {
					cmd[0] = w0;
					for (unsigned w = 1; w < ACPM_CMD_WORDS; w++)
						cmd[w] = read32(rslot + w*sizeof(uint32_t));

					rx_rear = (rx_rear + 1) % ch.q_len;
					write32(ch.rx_rear, rx_rear);
					write32(ch.rx_rear, rx_front);
					_mbox_write(MBOX_INTCR0, 1u << ch.id);
					return true;
				}
				rx_rear = (rx_rear + 1) % ch.q_len;
			}
			write32(ch.rx_rear, rx_front);
			_mbox_write(MBOX_INTCR0, 1u << ch.id);
		}

		return false;
	}

	/**
	 * Update the bits of a PMIC register that are selected by 'mask'
	 */
	bool _pmic_update(uint8_t reg, uint8_t val, uint8_t mask, uint8_t channel)
	{
		uint32_t cmd[ACPM_CMD_WORDS] = { 0 };

		cmd[0] = ((uint32_t)reg)
		       | ((uint32_t)PMIC_TYPE_PM << PMIC_TYPE_SHIFT)
		       | ((uint32_t)(channel & PMIC_CHANNEL_MASK) << PMIC_CHANNEL_SHIFT);
		cmd[1] = (uint32_t)ACPM_FUNC_UPDATE
		       | ((uint32_t)val  << ACPM_UPDATE_VALUE_SHIFT)
		       | ((uint32_t)mask << ACPM_UPDATE_MASK_SHIFT);

		if (!_send(_pmic_ch, cmd))
			return false;

		return ((cmd[1] >> ACPM_STATUS_SHIFT) & ACPM_STATUS_MASK) == 0;
	}

	/*
	 * Supplies of the panel: vci is LDO27 of the S2MPG12, vddi is BUCKA of
	 * the S2MPG13. The boot loader leaves both on. They are never switched
	 * off here because BUCKA may supply more than the panel, and switching
	 * it off would blank the display.
	 */
	struct Panel_power : Driver::Power
	{
		Acpm &_acpm;

		enum {
			S2MPG12_L27M_CTRL  = 0x46, L27M_EN_MASK  = 0x80,
			S2MPG13_BUCKA_CTRL = 0x26, BUCKA_EN_MASK = 0xC0,
		};

		Panel_power(Powers &powers, Acpm &acpm)
		:
			Driver::Power(powers, "panel-power"), _acpm(acpm)
		{ }

		void _on() override
		{
			bool const ok =
				_acpm._pmic_update(S2MPG12_L27M_CTRL, L27M_EN_MASK,
				                   L27M_EN_MASK, PMIC_CHANNEL)
			 && _acpm._pmic_update(S2MPG13_BUCKA_CTRL, BUCKA_EN_MASK,
				                   BUCKA_EN_MASK, PMIC_CHANNEL_SUB);
			if (!ok)
				Genode::warning("could not switch on the panel supplies");
		}

		void _off() override { }
	};

	Panel_power _panel_power { _powers, *this };

	/*
	 * Supplies of the touchscreen: LDO25 and LDO26 of the S2MPG12, given
	 * 5 ms to settle after switching them on
	 */
	struct Touch_power : Driver::Power
	{
		Acpm &_acpm;

		enum {
			S2MPG12_L25M_CTRL = 0x44, L25M_EN_MASK = 0x80,
			S2MPG12_L26M_CTRL = 0x45, L26M_EN_MASK = 0xC0,
			SETTLE_MS = 5,
		};

		Touch_power(Powers &powers, Acpm &acpm)
		:
			Driver::Power(powers, "touch-power"), _acpm(acpm)
		{ }

		void _on() override
		{
			bool const ok =
				_acpm._pmic_update(S2MPG12_L25M_CTRL, L25M_EN_MASK,
				                   L25M_EN_MASK, PMIC_CHANNEL)
			 && _acpm._pmic_update(S2MPG12_L26M_CTRL, L26M_EN_MASK,
				                   L26M_EN_MASK, PMIC_CHANNEL);
			if (!ok)
				Genode::warning("could not switch on the touchscreen supplies");

			_acpm._timer.msleep(SETTLE_MS);
		}

		void _off() override
		{
			_acpm._pmic_update(S2MPG12_L26M_CTRL, 0, L26M_EN_MASK, PMIC_CHANNEL);
			_acpm._pmic_update(S2MPG12_L25M_CTRL, 0, L25M_EN_MASK, PMIC_CHANNEL);
		}
	};

	/*
	 * Supplies of the USB PHYs: LDO7M to LDO10M of the S2MPG12, switched in
	 * LDO_CTRL1 with two mode bits each, 1 (on while awake) for LDO7M and
	 * LDO8M and 3 (on) for LDO9M and LDO10M, as the vendor's device tree
	 * sets them up. They stay on, the boot loader leaves them on as well.
	 */
	struct Usb_rails : Driver::Power
	{
		Acpm &_acpm;

		enum { S2MPG12_LDO_CTRL1 = 0x48, LDO_CTRL1_ON = 0xf5, SETTLE_MS = 1 };

		Usb_rails(Powers &powers, Acpm &acpm)
		:
			Driver::Power(powers, "usb-rails"), _acpm(acpm)
		{ }

		void _on() override
		{
			if (!_acpm._pmic_update(S2MPG12_LDO_CTRL1, LDO_CTRL1_ON, 0xff,
			                        PMIC_CHANNEL))
				Genode::warning("could not switch on the USB supplies");

			_acpm._timer.msleep(SETTLE_MS);
		}

		void _off() override { }
	};

	Timer::Connection _timer { _env };

	Touch_power _touch_power { _powers, *this };
	Usb_rails   _usb_rails   { _powers, *this };

	Acpm(Genode::Env &env, Powers &powers) : _env(env), _powers(powers)
	{
		_init_ipc();
	}
};

#endif /* _SRC__DRIVER__PLATFORM__GS201__ACPM_H_ */
