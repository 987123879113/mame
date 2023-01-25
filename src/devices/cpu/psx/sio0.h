// license:BSD-3-Clause
// copyright-holders:smf
/*
 * PlayStation Controller/Memory Card Serial I/O emulator
 *
 * Copyright 2003-2011 smf
 *
 */

#ifndef MAME_CPU_PSX_SIO0_H
#define MAME_CPU_PSX_SIO0_H

#pragma once


DECLARE_DEVICE_TYPE(PSX_SIO0, psxsio0_device)

#define SIO0_BUF_SIZE ( 8 )

#define SIO0_STATUS_TX_RDY ( 1 << 0 )
#define SIO0_STATUS_RX_RDY ( 1 << 1 )
#define SIO0_STATUS_TX_EMPTY ( 1 << 2 )
#define SIO0_STATUS_OVERRUN ( 1 << 4 )
#define SIO0_STATUS_DSR ( 1 << 7 )
#define SIO0_STATUS_IRQ ( 1 << 9 )

#define SIO0_CONTROL_TX_ENA ( 1 << 0 )
#define SIO0_CONTROL_IACK ( 1 << 4 )
#define SIO0_CONTROL_RESET ( 1 << 6 )
#define SIO0_CONTROL_TX_IENA ( 1 << 10 )
#define SIO0_CONTROL_RX_IENA ( 1 << 11 )
#define SIO0_CONTROL_DSR_IENA ( 1 << 12 )
#define SIO0_CONTROL_DTR ( 1 << 13 )

class psxsio0_device : public device_t
{
public:
	psxsio0_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// configuration helpers
	auto irq_handler() { return m_irq_handler.bind(); }
	auto sck_handler() { return m_sck_handler.bind(); }
	auto txd_handler() { return m_txd_handler.bind(); }
	auto dtr_handler() { return m_dtr_handler.bind(); }
	auto rts_handler() { return m_rts_handler.bind(); }

	void write(offs_t offset, uint32_t data, uint32_t mem_mask = ~0);
	uint32_t read(offs_t offset, uint32_t mem_mask = ~0);

	DECLARE_WRITE_LINE_MEMBER(write_rxd);
	DECLARE_WRITE_LINE_MEMBER(write_dsr);

protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_post_load() override;

	TIMER_CALLBACK_MEMBER( sio0_tick );

private:
	void sio0_interrupt();
	void sio0_timer_adjust();

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

#endif // MAME_CPU_PSX_SIO0_H
