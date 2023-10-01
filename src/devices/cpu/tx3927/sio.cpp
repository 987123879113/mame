// license:BSD-3-Clause
// copyright-holders:windyfairy

/*
 * Toshiba TX3927 emulation.
 * Based on MIPS I with extensions and peripherals.
 */

#include "emu.h"
#include "cpu/tx3927/sio.h"

#define LOG_TX39_SIO (1U << 5)

#include <iostream>

//#define VERBOSE (LOG_TX39_SIO)
#define LOG_OUTPUT_STREAM std::cout

#include "logmacro.h"

DEFINE_DEVICE_TYPE(TX3927_SIO, tx3927_sio, "tx3927_sio", "Toshiba TX3927 Serial I/O")

tx3927_sio::tx3927_sio(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock) :
	device_t(mconfig, TX3927_SIO, tag, owner, clock),
	device_serial_interface(mconfig, *this),
	m_irq_handler(*this),
	m_txd_handler(*this),
	m_dtr_handler(*this),
	m_rts_handler(*this)
{
}

void tx3927_sio::device_start()
{
	m_timer = timer_alloc(FUNC(tx3927_sio::sio_tick), this);
}

void tx3927_sio::device_reset()
{
	m_sifcr = 0;
	m_sidisr = 0x2000;

	m_silcr = 0;
	m_sidicr = 0;
	m_siscisr = 0b000110;
	m_siflcr = 0b0000000110000010;
	m_sibgr = 0x3ff;

	m_data_bits_count = 8;

	std::fill(std::begin(m_sitfifo), std::end(m_sitfifo), 0);
	m_sitfifo_len = 0;

	std::fill(std::begin(m_sirfifo), std::end(m_sirfifo), 0);
	m_sirfifo_len = 0;
}

TIMER_CALLBACK_MEMBER(tx3927_sio::sio_tick)
{
	transmit_clock(false);
	sio_timer_adjust();
}

void tx3927_sio::transmit_clock(bool is_cts)
{
	// TODO: Support transmit enable select where CTS* hardware signal is used

	if (is_transmit_register_empty() && m_sitfifo_len > 0)
	{
		// If TSDE is set then SIO halts transmission until the bit is cleared
		if (BIT(m_siflcr, SIFLCR_TSDE))
			return;

		auto r = m_sitfifo[0];
		std::copy(std::begin(m_sitfifo) + 1, std::end(m_sitfifo), std::begin(m_sitfifo));
		m_sitfifo_len--;
		transmit_register_setup(r);
	}

	// Bits are only transmitted every 16 SIOCLK cycles
	if (m_transmit_bit != 0) {
		if (m_transmit_bit == 15) {
			m_transmit_bit = 0;
		}

		return;
	}
	else {
		m_transmit_bit++;
	}

	if (!is_transmit_register_empty())
	{
		uint8_t data = transmit_register_get_data_bit();
		LOGMASKED(LOG_TX39_SIO, "Tx Present a %d\n", data);
		m_txd_handler(data);
	}
}

void tx3927_sio::write_rxd(int state)
{
	if (!BIT(m_siflcr, SIFLCR_RSDE)) {
		return;
	}
	// TODO?: The receiver controller looks for the high-to-low transition of a start bit on the RXD pin. A Low on
	// RXD is not treated as a start bit at the time when the SIFLCR.RSDE bit is cleared.When a valid start
	// bit has been detected, the receive controller begins sampling data received on the RXD pin

	LOGMASKED(LOG_TX39_SIO, "sio: Presented a %02x\n", state);

	m_recv_timeout_counter = machine().time();

	receive_register_update_bit(state);

	if (is_receive_register_full())
	{
		receive_register_extract();

		auto c = get_received_char();
		auto status = 0;

		if (is_receive_parity_error()) {
			status |= 1 << (SIDISR_UPER - SIDISR_UOER);
		}

		if (is_receive_framing_error()) {
			status |= 1 << (SIDISR_UFER - SIDISR_UOER);

			m_siscisr |= 1 << SISCISR_UBRKD;
			m_siscisr |= 1 << SISCISR_RBRKD;

			if (BIT(m_sidicr, SIDICR_STIE_UBRKD) || BIT(m_sidicr, SIDICR_STIE_RBRKD)) {
				// Set STIS when UBRKD or RBRKD is triggered
				m_sidisr |= 1 << SIDISR_STIS;
			}
		}
		else {
			// Automatically cleared when a non-break frame is received
			m_siscisr |= ~(1 << SISCISR_RBRKD);
		}

		if (m_sirfifo_len < 16) {
			m_sirfifo[m_sirfifo_len] = (status << 8) | c;
			m_sirfifo_len++;
		}
		else {
			// Overrun status bit of the 16th byte in the receive FIFO is set when the buffer is 100% full
			m_sirfifo[15] |= 1 << (SIDISR_UOER - SIDISR_UOER);
		}

		// TODO: Set flags as required for received byte(?)
		m_siflcr |= 1 << SIFLCR_RTSSC; // Software control RTS

		if (BIT(m_siflcr, SIFLCR_RCS) && m_sirfifo_len >= BIT(m_siflcr, SIFLCR_RTSTL, 4) && BIT(m_siflcr, SIFLCR_RTSTL, 4) != 0) {
			// Also needs hardware control RTS to be triggered
			m_rts_handler(1);
		}
	}
}

void tx3927_sio::write_cts(int state)
{
	if (BIT(m_sidicr, SIDICR_STIE_CTSAC) && BIT(m_sidicr, SIDICR_CTSAC, 2) != 0) {
		bool t;

		if (BIT(m_sidicr, SIDICR_CTSAC, 2) == 1) {
			// Falling edge on the CTS* pin
			t = m_cts && !state;
		}
		else if (BIT(m_sidicr, SIDICR_CTSAC, 2) == 2) {
			// Rising edge on the CTS* pin
			t = !m_cts && state;
		}
		else {
			// Both rising and falling edges on the CTS* pin
			t = true;
		}

		if (t) // Sets STIS to 1 when the change specified by CTSAC occurs in CTSS
			m_sidisr |= 1 << SIDISR_STIS;
	}

	m_cts = state;
	m_siscisr |= 1 << SISCISR_CTSS;
	transmit_clock(true);
}

void tx3927_sio::sio_timer_adjust()
{
	constexpr int clock_divs[4] = { 2, 8, 32, 128 };
	const auto clock_div = clock_divs[BIT(m_sibgr, 8, 2)];
	const auto brd_div = m_sibgr & 0xff;
	const auto imclk = attotime::from_hz(133000000.0 / 4);
	const auto sclk = attotime::from_hz(133000000.0 / 4);
	const auto target_clock = BIT(m_clock_sel, 1) ? sclk : imclk;

	if (BIT(m_clock_sel, 0)) {
		attotime n_time = attotime::never;

		// Baud rate generator
		if (clock_div != 0 && brd_div != 0)
		{
			n_time = attotime::from_hz(target_clock.as_hz() / clock_div / brd_div / 16);
			//LOGMASKED(LOG_TX39_SIO, "sio_timer_adjust( %s ) = %s ( %d x %d )\n", tag(), n_time.as_string(), clock_div, brd_div);
		}
		else {
			//LOGMASKED(LOG_TX39_SIO, "sio_timer_adjust( %s ) invalid baud rate ( %d x %d )\n", tag(), clock_div, brd_div);
		}

		m_timer->adjust(n_time);
	}
	else {
		// Internal/external clock
		m_timer->adjust(target_clock);
	}
}

uint32_t tx3927_sio::read(offs_t offset, uint32_t mem_mask)
{
	auto sio_offset = (offset & 0x3f) * 4;

	//LOGMASKED(LOG_TX39_SIO, "%s: sio_read %08x %08x | %04x\n", machine().describe_context().c_str(), offset * 4, mem_mask, sio_offset);

	switch (sio_offset) {
	case 0x00:
		return m_silcr;

	case 0x04:
		return m_sidicr;

	case 0x08: {
		m_sidisr &= ~0x0f;
		m_sidisr |= m_sirfifo_len;

		if (8 - m_sitfifo_len > 0) {
			// Transmit Data Empty (has at least 1 empty location)
			m_siscisr |= 1 << SISCISR_TRDY;

			if (BIT(m_sidicr, SIDICR_STIE_TRDY)) // Sets STIS to 1 when TRDY is set
				m_sidisr |= 1 << SIDISR_STIS;
		}
		else {
			m_siscisr &= ~(1 << SISCISR_TRDY);
		}

		if (m_sitfifo_len <= 0 && is_transmit_register_empty()) {
			// Transmission Complete
			m_siscisr |= 1 << SISCISR_TXALS;

			if (BIT(m_sidicr, SIDICR_STIE_TXALS)) // Sets STIS to 1 when TXALS is set
				m_sidisr |= 1 << SIDISR_STIS;
		}
		else {
			m_siscisr &= ~(1 << SISCISR_TXALS);
		}

		const int sitfifo_len_limits[4] = { 1, 4, 8, 0 };
		if (BIT(m_sifcr, SIFCR_TDIL, 2) != 3 && 8 - m_sitfifo_len >= sitfifo_len_limits[BIT(m_sifcr, SIFCR_TDIL, 2)]) {
			// Transmit Data Empty
			m_sidisr |= 1 << SIDISR_TDIS;

			if (BIT(m_sidicr, SIDICR_TIE) && !BIT(m_sidicr, SIDICR_TDE)) {
				// Assert SITXIREQ (IRQ)
			}
			else if (!BIT(m_sidicr, SIDICR_TIE) && BIT(m_sidicr, SIDICR_TDE)) {
				// Assert SITXDREQ (DMA)
			}
		}

		const int sirfifo_len_limits[4] = { 1, 4, 8, 12 };
		if (m_sirfifo_len >= sirfifo_len_limits[BIT(m_sifcr, SIFCR_RDIL, 2)]) {
			// Receive Data Full
			m_sidisr |= 1 << SIDISR_RDIS;

			if (BIT(m_sidicr, SIDICR_RIE) && !BIT(m_sidicr, SIDICR_RDE)) {
				// Assert SIRXIREQ (IRQ)
			}
			else if (!BIT(m_sidicr, SIDICR_RIE) && BIT(m_sidicr, SIDICR_RDE)) {
				// Assert SIRXDREQ (DMA)
			}
		}

		//LOGMASKED(LOG_TX39_SIO, "sio data %08x\n", m_sidisr);

		return m_sidisr;
	}

	case 0x0c:
		if (8 - m_sitfifo_len > 0) {
			// Transmit Data Empty (has at least 1 empty location)
			m_siscisr |= 1 << SISCISR_TRDY;

			if (BIT(m_sidicr, SIDICR_STIE_TRDY)) // Sets STIS to 1 when TRDY is set
				m_sidisr |= 1 << SIDISR_STIS;
		}
		else {
			m_siscisr &= ~(1 << SISCISR_TRDY);
		}

		if (m_sitfifo_len <= 0 && is_transmit_register_empty()) {
			// Transmission Complete
			m_siscisr |= 1 << SISCISR_TXALS;

			if (BIT(m_sidicr, SIDICR_STIE_TXALS)) // Sets STIS to 1 when TXALS is set
				m_sidisr |= 1 << SIDISR_STIS;
		}
		else {
			m_siscisr &= ~(1 << SISCISR_TXALS);
		}

		//LOGMASKED(LOG_TX39_SIO, "sio data %08x\n", m_siscisr);

		return m_siscisr;
	case 0x10:
		return m_sifcr;

	case 0x14:
		return m_siflcr;

	case 0x18:
		return m_sibgr;

	case 0x20: {
		uint32_t r = 0;

		if (m_sirfifo_len > 0) {
			r = m_sirfifo[0] & 0xff;

			auto status = m_sirfifo[0] >> 8;
			std::copy(std::begin(m_sirfifo) + 1, std::end(m_sirfifo), std::begin(m_sirfifo));
			m_sirfifo_len--;

			if (BIT(status, SIDISR_UOER - SIDISR_UOER)) {
				m_sidisr |= 1 << SIDISR_UOER;
				m_sidisr |= 1 << SIDISR_ERI;
			}
			else {
				m_sidisr &= ~(1 << SIDISR_UOER);
			}

			if (BIT(status, SIDISR_UPER - SIDISR_UOER)) {
				m_sidisr |= 1 << SIDISR_UPER;
				m_sidisr |= 1 << SIDISR_ERI;
			}
			else {
				m_sidisr &= ~(1 << SIDISR_UPER);
			}

			if (BIT(status, SIDISR_UFER - SIDISR_UOER)) {
				m_sidisr |= 1 << SIDISR_UFER;
				m_sidisr |= 1 << SIDISR_ERI;
			}
			else {
				m_sidisr &= ~(1 << SIDISR_UFER);
			}

			if (BIT(status, SIDISR_UBRK - SIDISR_UOER)) {
				m_sidisr |= 1 << SIDISR_UBRK;
			}
			else {
				m_sidisr &= ~(1 << SIDISR_UBRK);
			}

			if (m_sirfifo_len > 0) {
				m_sidisr |= 1 << SIDISR_UVALID;
			}
			else {
				m_sidisr &= ~(1 << SIDISR_UVALID);
			}
		}
		else {
			// Error Interrupt
			m_sidisr |= 1 << SIDISR_ERI;
		}

		if (!BIT(m_sidicr, SIDICR_RDE) && BIT(m_sidicr, SIDICR_RIE) && (BIT(m_sidisr, SIDISR_ERI) || BIT(m_sidisr, SIDISR_TOUT))) {
			// TODO: Receive data serial interrupt
			//m_status |= SIO_STATUS_IRQ;
			m_irq_handler(1);
		}
		else if (BIT(m_sidicr, SIDICR_RDE) && !BIT(m_sidicr, SIDICR_RIE) && (BIT(m_sidisr, SIDISR_RDIS) || BIT(m_sidisr, SIDISR_TOUT))) {
			// TODO: Receive data DMA interrupt
		}

		if (BIT(m_sidicr, SIDICR_SPIE) && BIT(m_sidisr, SIDISR_ERI)) {
			// TODO: Assert SISPIREQ
		}

		return r;
	}
	}

	return 0;
}

void tx3927_sio::write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	auto sio_offset = (offset & 0x3f) * 4;

	LOGMASKED(LOG_TX39_SIO, "%s: sio_write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 4, data, mem_mask);

	switch (sio_offset) {
	case 0x00:
	{
		//auto txd_open_drain_enable = BIT(m_silcr, 13); // Multidrop only
		//auto transmit_wakeup_bit = BIT(m_silcr, 14); // Multidrop only
		//auto receive_wakeup_bit = BIT(m_silcr, 15); // Multidrop only

		auto stop_bits = BIT(data, 2) ? STOP_BITS_2 : STOP_BITS_1;
		auto parity_enabled = BIT(data, 3) && !BIT(data, 1);
		auto parity = parity_enabled ? (BIT(data, 4) ? PARITY_EVEN : PARITY_ODD) : PARITY_ODD;
		m_data_bits_count = BIT(data, 0) ? 7 : 8;
		set_data_frame(1, m_data_bits_count, parity, stop_bits);

		if (BIT(data, 5, 2) != m_clock_sel) {
			m_clock_sel = BIT(data, 5, 2);
			sio_timer_adjust();
		}

		m_silcr = data;
		break;
	}

	case 0x04:
		m_sidicr = data;
		break;

	case 0x08:
		m_sidisr = (m_sidisr & 0xf800) | (data & ~0xf800);
		break;

	case 0x0c:
		m_siscisr = (m_siscisr & ~0x21) | (data & 0x21);
		break;

	case 0x10: {
		if (BIT(data, SIFCR_SWRST)) {
			// TODO: SIO reset
			data &= ~(1 << SIFCR_SWRST);
		}

		if (BIT(data, SIFCR_FRSTE) && BIT(data, SIFCR_TFRST)) {
			// Transmit FIFO reset
			data &= ~(1 << SIFCR_TFRST);
			m_sitfifo_len = 0;
		}

		if (BIT(data, SIFCR_FRSTE) && BIT(data, SIFCR_RFRST)) {
			// Receive FIFO reset
			data &= ~(1 << SIFCR_RFRST);
			m_sirfifo_len = 0;
		}

		m_sifcr = data;
		break;
	}

	case 0x14:
		m_siflcr = data;
		break;

	case 0x18:
		if (data != m_sibgr) {
			m_sibgr = data;
			sio_timer_adjust();
		}
		break;

	case 0x1c:
		LOGMASKED(LOG_TX39_SIO, "sio_write %08x %c\n", data, data & 0xff);

		if (m_sitfifo_len < 8) {
			//m_sitfifo[m_sitfifo_len++] = data;
		}

		break;
	}
}
