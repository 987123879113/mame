// license:BSD-3-Clause
// copyright-holders:smf
/*
 * PlayStation Serial I/O emulator
 *
 * Copyright 2003-2011 smf
 *
 */

#ifndef MAME_CPU_PSX_SIO_H
#define MAME_CPU_PSX_SIO_H

#pragma once

#include "diserial.h"


DECLARE_DEVICE_TYPE(PSX_SIO0, psxsio0_device)
DECLARE_DEVICE_TYPE(PSX_SIO1, psxsio1_device)

#define SIO_BUF_SIZE ( 8 )

#define SIO_STATUS_TX_RDY ( 1 << 0 )
#define SIO_STATUS_RX_RDY ( 1 << 1 )
#define SIO_STATUS_TX_EMPTY ( 1 << 2 )
#define SIO_STATUS_PARITY_ERROR ( 1 << 3 )
#define SIO_STATUS_OVERRUN_ERROR ( 1 << 4 )
#define SIO_STATUS_OVERRUN ( 1 << 4 )
#define SIO_STATUS_FRAMING_ERROR ( 1 << 5 )
#define SIO_STATUS_RX ( 1 << 6 )
#define SIO_STATUS_DSR ( 1 << 7 )
#define SIO_STATUS_CTS ( 1 << 8 )
#define SIO_STATUS_IRQ ( 1 << 9 )

#define SIO_CONTROL_BIT_TXEN 0
#define SIO_CONTROL_BIT_DTR 1
#define SIO_CONTROL_BIT_RXEN 2
#define SIO_CONTROL_BIT_TX 3
#define SIO_CONTROL_BIT_IACK 4
#define SIO_CONTROL_BIT_RTS 5
#define SIO_CONTROL_BIT_RESET 6
#define SIO_CONTROL_BIT_TX_IENA 10
#define SIO_CONTROL_BIT_RX_IENA 11
#define SIO_CONTROL_BIT_DSR_IENA 12

#define SIO_CONTROL_TX_ENA ( 1 << SIO_CONTROL_BIT_TXEN )
#define SIO_CONTROL_TXEN ( 1 << SIO_CONTROL_BIT_TXEN )
#define SIO_CONTROL_DTR ( 1 << SIO_CONTROL_BIT_DTR )
#define SIO_CONTROL_RXEN ( 1 << SIO_CONTROL_BIT_RXEN )
#define SIO_CONTROL_TX ( 1 << SIO_CONTROL_BIT_TX )
#define SIO_CONTROL_IACK ( 1 << SIO_CONTROL_BIT_IACK )
#define SIO_CONTROL_RTS ( 1 << SIO_CONTROL_BIT_RTS )
#define SIO_CONTROL_RESET ( 1 << SIO_CONTROL_BIT_RESET )
#define SIO_CONTROL_RX_IMODE (( 1 << 8 ) | ( 1 << 9 ))
#define SIO_CONTROL_TX_IENA ( 1 << SIO_CONTROL_BIT_TX_IENA )
#define SIO_CONTROL_RX_IENA ( 1 << SIO_CONTROL_BIT_RX_IENA )
#define SIO_CONTROL_DSR_IENA ( 1 << SIO_CONTROL_BIT_DSR_IENA )
//#define SIO_CONTROL_DTR ( 1 << 13 )


class psxsio0_device : public device_t
{
public:
	// configuration helpers
	auto irq_handler() { return m_irq_handler.bind(); }
	auto sck_handler() { return m_sck_handler.bind(); }
	auto txd_handler() { return m_txd_handler.bind(); }
	auto dtr_handler() { return m_dtr_handler.bind(); }
	auto rts_handler() { return m_rts_handler.bind(); }

	psxsio0_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	void write(offs_t offset, uint32_t data, uint32_t mem_mask = ~0);
	uint32_t read(offs_t offset, uint32_t mem_mask = ~0);

	void write_rxd(int state);
	void write_dsr(int state);

protected:
	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_post_load() override;

	TIMER_CALLBACK_MEMBER( sio_tick );

private:
	void sio_interrupt();
	void sio_timer_adjust();

	uint32_t m_status;
	uint32_t m_mode;
	uint32_t m_control;
	uint32_t m_baud;
	int m_rxd;
	uint32_t m_tx_data;
	uint32_t m_rx_data;
	uint32_t m_tx_shift;
	uint32_t m_rx_shift;
	uint32_t m_tx_bits;
	uint32_t m_rx_bits;

	emu_timer *m_timer;

	devcb_write_line m_irq_handler;
	devcb_write_line m_sck_handler;
	devcb_write_line m_txd_handler;
	devcb_write_line m_dtr_handler;
	devcb_write_line m_rts_handler;
};


class psxsio1_device : public device_t,
	public device_serial_interface
{
public:
	psxsio1_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock);

	auto irq_handler() { return m_irq_handler.bind(); }
	auto txd_handler() { return m_txd_handler.bind(); }
	auto dtr_handler() { return m_dtr_handler.bind(); }
	auto rts_handler() { return m_rts_handler.bind(); }

	uint8_t data_r();
	void data_w(uint8_t data);
	uint16_t status_r();

	virtual uint32_t read(offs_t offset, uint32_t mem_mask);
	virtual void write(offs_t offset, uint32_t data, uint32_t mem_mask);

	void write_rxd(int state);
	void write_cts(int state);
	void write_dsr(int state);

protected:
	psxsio1_device(
		const machine_config& mconfig,
		device_type type,
		const char* tag,
		device_t* owner,
		uint32_t clock);

	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_post_load() override;

	TIMER_CALLBACK_MEMBER( sio_tick );

	void sio_timer_adjust();

	void command_w(uint16_t data);
	void mode_w(uint16_t data);

	void receive_character(uint8_t ch);

	void transmit_clock();
	bool is_tx_enabled() const;
	void check_for_tx_start();
	void start_tx();

private:
	devcb_write_line m_irq_handler;
	devcb_write_line m_txd_handler;
	devcb_write_line m_dtr_handler;
	devcb_write_line m_rts_handler;

	uint16_t m_status;
	uint16_t m_control;
	uint16_t m_mode;
	uint32_t m_baud;
	bool m_delayed_tx_en;

	bool m_cts;
	bool m_dsr;
	bool m_rxd;
	int m_txc_count;
	int m_br_factor;

	uint8_t m_rx_data;
	uint8_t m_tx_data;
	u8 m_rxd_bits;
	u8 m_data_bits_count;

	emu_timer* m_timer;
};

#endif // MAME_CPU_PSX_SIO_H
