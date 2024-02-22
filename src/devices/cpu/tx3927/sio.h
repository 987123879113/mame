// license:BSD-3-Clause
// copyright-holders:windyfairy

#ifndef MAME_CPU_MIPS_TX3927_SIO_H
#define MAME_CPU_MIPS_TX3927_SIO_H

#pragma once

#include "diserial.h"

class tx3927_sio : public device_t,
	public device_serial_interface
{
public:
	// construction/destruction
	tx3927_sio(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock);

	// configuration helpers
	auto irq_handler() { return m_irq_handler.bind(); }
	auto txd_handler() { return m_txd_handler.bind(); }
	auto dtr_handler() { return m_dtr_handler.bind(); }
	auto rts_handler() { return m_rts_handler.bind(); }

	virtual void device_start() override;
	virtual void device_reset() override;

	void write_rxd(int state);
	void write_cts(int state);

	uint32_t read(offs_t offset, uint32_t mem_mask);
	void write(offs_t offset, uint32_t data, uint32_t mem_mask);

	TIMER_CALLBACK_MEMBER(sio_tick);

private:
	enum : u32 {
		SIFCR_SWRST = 15, // Software Reset
		SIFCR_RDIL = 7, // Receive FIFO Request Trigger Level
		SIFCR_TDIL = 3, // Transmit FIFO Request Trigger Level
		SIFCR_TFRST = 2, // Transmit FIFO Reset
		SIFCR_RFRST = 1, // Receive FIFO Reset
		SIFCR_FRSTE = 0, // FIFO Reset Enable

		SIDISR_UBRK = 15, // UART Break Reception
		SIDISR_UVALID = 14, // UART Receiver FIFO Available Status
		SIDISR_UFER = 13, // UART Frame Error
		SIDISR_UPER = 12, // UART Parity Error
		SIDISR_UOER = 11, // UART Overrun Error
		SIDISR_ERI = 10, // Error Interrupt
		SIDISR_TOUT = 9, // Receive Timeout
		SIDISR_TDIS = 8, // Transmit Data Empty
		SIDISR_RDIS = 7, // Receive Data Full
		SIDISR_STIS = 6, // Status Change Interrupt status

		SIDICR_TDE = 15, // Transmit DMA Enable (SITXDREQ)
		SIDICR_RDE = 14, // Receive DMA Enable (SIRXDREQ)
		SIDICR_TIE = 13, // Transmit Data Interrupt Enable (SITXIREQ)
		SIDICR_RIE = 12, // Receive Data Interrupt Enable (SIRXIREQ)
		SIDICR_SPIE = 11, // Special Receive Interrupt Enable (SISPIREQ)
		SIDICR_CTSAC = 9, // CTSS Active Condition (2 bits)
		SIDICR_STIE_OERS = 5,
		SIDICR_STIE_CTSAC = 4,
		SIDICR_STIE_RBRKD = 3,
		SIDICR_STIE_TRDY = 2,
		SIDICR_STIE_TXALS = 1,
		SIDICR_STIE_UBRKD = 0,

		SIFLCR_RCS = 12, // RTS Control Select
		SIFLCR_TES = 11, // Transmit Enable Select
		SIFLCR_RTSSC = 9, // RTS Software Control
		SIFLCR_RSDE = 8, // Receive Serial Data Enable
		SIFLCR_TSDE = 7, // Transmit Serial Data Enable
		SIFLCR_RTSTL = 1, // RTS Active Trigger Level
		SIFLCR_TBRK = 0, // Break Transmit


		SISCISR_OERS = 5, // Overrun Error Status
		SISCISR_CTSS = 4, // CTS* Terminal Status
		SISCISR_RBRKD = 3, // Receive Break
		SISCISR_TRDY = 2, // Transmit Data Empty
		SISCISR_TXALS = 1, // Transmission Completed
		SISCISR_UBRKD = 0, // UART Break Detection
	};

	devcb_write_line m_irq_handler;
	devcb_write_line m_txd_handler;
	devcb_write_line m_dtr_handler;
	devcb_write_line m_rts_handler;

	void sio_timer_adjust();
	void transmit_clock(bool is_cts);
	void start_tx();

	emu_timer* m_timer;

	uint32_t m_sifcr = 0;
	uint32_t m_sidisr = 0x2000; // DMA/Interrupt Status Registers (SIDISRn)

	uint32_t m_silcr = 0;
	uint32_t m_sidicr = 0;
	uint32_t m_siscisr = 0b000110;
	uint32_t m_siflcr = 0b0000000110000010;
	uint32_t m_sibgr = 0x3ff;
	uint8_t m_sitfifo[8] = { 0 };
	uint32_t m_sitfifo_len = 0;
	uint16_t m_sirfifo[16] = { 0 };
	uint32_t m_sirfifo_len = 0;

	uint8_t m_rx_data = 0;
	uint8_t m_tx_data = 0;
	int m_data_bits_count = 0;

	int m_cts = 0;

	// Clock select values:
	// 0 = internal (IMCLK, 1/4th of 133MHz CPU clock)
	// 1 = baud rate generator (IMCLK)
	// 2 = external clock (SCLK)
	// 3 = baud rate generator (SCLK)
	int m_clock_sel = 0;

	attotime m_recv_timeout_counter = attotime::never;
	int m_transmit_bit = 0;
};

DECLARE_DEVICE_TYPE(TX3927_SIO,  tx3927_sio)

#endif // MAME_CPU_MIPS_TX3927_SIO_H
