// license:BSD-3-Clause
// copyright-holders:smf
/*
 * PlayStation Controller/Memory Card Serial I/O emulator
 *
 * Copyright 2003-2011 smf
 *
 */

#include "emu.h"
#include "sio0.h"

#include <cstdarg>

#define VERBOSE_LEVEL ( 0 )

static inline void ATTR_PRINTF(3,4) verboselog( device_t& device, int n_level, const char *s_fmt, ... )
{
	if( VERBOSE_LEVEL >= n_level )
	{
		va_list v;
		char buf[ 32768 ];
		va_start( v, s_fmt );
		vsprintf( buf, s_fmt, v );
		va_end( v );
		device.logerror( "%s: %s", device.machine().describe_context(), buf );
	}
}

DEFINE_DEVICE_TYPE(PSX_SIO0, psxsio0_device, "psxsio0", "Sony PSX SIO-0")

psxsio0_device::psxsio0_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, PSX_SIO0, tag, owner, clock),
	m_status(SIO0_STATUS_TX_EMPTY | SIO0_STATUS_TX_RDY), m_mode(0), m_control(0), m_baud(0),
	m_rxd(1), m_tx_data(0), m_rx_data(0), m_tx_shift(0), m_rx_shift(0), m_tx_bits(0), m_rx_bits(0), m_timer(nullptr),
	m_irq_handler(*this),
	m_sck_handler(*this),
	m_txd_handler(*this),
	m_dtr_handler(*this),
	m_rts_handler(*this)
{
}

void psxsio0_device::device_post_load()
{
	sio0_timer_adjust();
}

void psxsio0_device::device_start()
{
	m_irq_handler.resolve_safe();
	m_sck_handler.resolve_safe();
	m_txd_handler.resolve_safe();
	m_dtr_handler.resolve_safe();
	m_rts_handler.resolve_safe();

	m_timer = timer_alloc( FUNC( psxsio0_device::sio0_tick ), this );
	m_mode = 0;
	m_control = 0;
	m_baud = 0;
	m_rx_data = 0;
	m_tx_data = 0;
	m_rx_shift = 0;
	m_tx_shift = 0;
	m_rx_bits = 0;
	m_tx_bits = 0;

	save_item( NAME( m_status ) );
	save_item( NAME( m_mode ) );
	save_item( NAME( m_control ) );
	save_item( NAME( m_baud ) );
	save_item( NAME( m_rxd ) );
	save_item( NAME( m_rx_data ) );
	save_item( NAME( m_tx_data ) );
	save_item( NAME( m_rx_shift ) );
	save_item( NAME( m_tx_shift ) );
	save_item( NAME( m_rx_bits ) );
	save_item( NAME( m_tx_bits ) );
}

void psxsio0_device::sio0_interrupt()
{
	verboselog( *this, 1, "sio0_interrupt( %s )\n", tag() );
	m_status |= SIO0_STATUS_IRQ;
	m_irq_handler(1);
}

void psxsio0_device::sio0_timer_adjust()
{
	attotime n_time;

	if( ( m_status & SIO0_STATUS_TX_EMPTY ) == 0 || m_tx_bits != 0 )
	{
		int n_prescaler;

		switch( m_mode & 3 )
		{
		case 1:
			n_prescaler = 1;
			break;
		case 2:
			n_prescaler = 16;
			break;
		case 3:
			n_prescaler = 64;
			break;
		default:
			n_prescaler = 0; // TODO: Should be 1 according to psx-spx, needs testing
			break;
		}

		if( m_baud != 0 && n_prescaler != 0 )
		{
			n_time = attotime::from_hz(33868800) * (n_prescaler * m_baud);
			verboselog( *this, 2, "sio0_timer_adjust( %s ) = %s ( %d x %d )\n", tag(), n_time.as_string(), n_prescaler, m_baud );
		}
		else
		{
			n_time = attotime::never;
			verboselog( *this, 0, "sio0_timer_adjust( %s ) invalid baud rate ( %d x %d )\n", tag(), n_prescaler, m_baud );
		}
	}
	else
	{
		n_time = attotime::never;
		verboselog( *this, 2, "sio0_timer_adjust( %s ) finished\n", tag() );
	}

	m_timer->adjust( n_time );
}

TIMER_CALLBACK_MEMBER( psxsio0_device::sio0_tick )
{
	verboselog( *this, 2, "sio0 tick\n" );

	if( m_tx_bits == 0 &&
		( m_control & SIO0_CONTROL_TX_ENA ) != 0 &&
		( m_status & SIO0_STATUS_TX_EMPTY ) == 0 )
	{
		m_tx_bits = 8;
		m_tx_shift = m_tx_data;

		m_rx_bits = 8;
		m_rx_shift = 0;

		m_status |= SIO0_STATUS_TX_EMPTY;
		m_status |= SIO0_STATUS_TX_RDY;
	}

	if( m_tx_bits != 0 )
	{
		m_sck_handler(0);

		m_txd_handler( m_tx_shift & 1 );
		m_tx_shift >>= 1;
		m_tx_bits--;

		m_sck_handler(1);

		if( m_tx_bits == 0 &&
			( m_control & SIO0_CONTROL_TX_IENA ) != 0 )
		{
			sio0_interrupt();
		}
	}

	if( m_rx_bits != 0 )
	{
		m_rx_shift = ( m_rx_shift >> 1 ) | ( m_rxd << 7 );
		m_rx_bits--;

		if( m_rx_bits == 0 )
		{
			if( ( m_status & SIO0_STATUS_RX_RDY ) != 0 )
			{
				m_status |= SIO0_STATUS_OVERRUN;
			}
			else
			{
				m_rx_data = m_rx_shift;
				m_status |= SIO0_STATUS_RX_RDY;
			}

			if( ( m_control & SIO0_CONTROL_RX_IENA ) != 0 )
			{
				sio0_interrupt();
			}
		}
	}

	sio0_timer_adjust();
}

void psxsio0_device::write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	switch( offset % 4 )
	{
	case 0:
		verboselog( *this, 1, "psx_sio0_w %s data %02x (%08x)\n", tag(), data, mem_mask );
		m_tx_data = data;
		m_status &= ~( SIO0_STATUS_TX_RDY );
		m_status &= ~( SIO0_STATUS_TX_EMPTY );
		sio0_timer_adjust();
		break;
	case 1:
		verboselog( *this, 0, "psx_sio0_w( %08x, %08x, %08x )\n", offset, data, mem_mask );
		break;
	case 2:
		if( ACCESSING_BITS_0_15 )
		{
			m_mode = data & 0xffff;
			verboselog( *this, 1, "psx_sio0_w %s mode %04x\n", tag(), data & 0xffff );
		}
		if( ACCESSING_BITS_16_31 )
		{
			verboselog( *this, 1, "psx_sio0_w %s control %04x\n", tag(), data >> 16 );
			m_control = data >> 16;

			if( ( m_control & SIO0_CONTROL_RESET ) != 0 )
			{
				verboselog( *this, 1, "psx_sio0_w reset\n" );
				m_status |= SIO0_STATUS_TX_EMPTY | SIO0_STATUS_TX_RDY;
				m_status &= ~( SIO0_STATUS_RX_RDY | SIO0_STATUS_OVERRUN | SIO0_STATUS_IRQ );
				m_irq_handler(0);

				// toggle DTR to reset controllers, Star Ocean 2, at least, requires it
				// the precise mechanism of the reset is unknown
				// maybe it's related to the bottom 2 bits of control which are usually set
				m_dtr_handler(0);
				m_dtr_handler(1);

				m_tx_bits = 0;
				m_rx_bits = 0;
				m_txd_handler(1);
			}
			if( ( m_control & SIO0_CONTROL_IACK ) != 0 )
			{
				verboselog( *this, 1, "psx_sio0_w iack\n" );
				m_status &= ~( SIO0_STATUS_IRQ );
				m_control &= ~( SIO0_CONTROL_IACK );
				m_irq_handler(0);
			}
			if( ( m_control & SIO0_CONTROL_DTR ) != 0 )
			{
				m_dtr_handler(0);
			}
			else
			{
				m_dtr_handler(1);
			}
		}
		break;
	case 3:
		if( ACCESSING_BITS_0_15 )
		{
			verboselog( *this, 0, "psx_sio0_w( %08x, %08x, %08x )\n", offset, data, mem_mask );
		}
		if( ACCESSING_BITS_16_31 )
		{
			m_baud = data >> 16;
			verboselog( *this, 1, "psx_sio0_w %s baud %04x\n", tag(), data >> 16 );
		}
		break;
	default:
		verboselog( *this, 0, "psx_sio0_w( %08x, %08x, %08x )\n", offset, data, mem_mask );
		break;
	}
}

uint32_t psxsio0_device::read(offs_t offset, uint32_t mem_mask)
{
	uint32_t data;

	switch( offset % 4 )
	{
	case 0:
		data = m_rx_data;
		m_status &= ~( SIO0_STATUS_RX_RDY );
		m_rx_data = 0xff;
		verboselog( *this, 1, "psx_sio0_r %s data %02x (%08x)\n", tag(), data, mem_mask );
		break;
	case 1:
		data = m_status;
		if( ACCESSING_BITS_0_15 )
		{
			verboselog( *this, 1, "psx_sio0_r %s status %04x\n", tag(), data & 0xffff );
		}
		if( ACCESSING_BITS_16_31 )
		{
			verboselog( *this, 0, "psx_sio0_r( %08x, %08x ) %08x\n", offset, mem_mask, data );
		}
		break;
	case 2:
		data = ( m_control << 16 ) | m_mode;
		if( ACCESSING_BITS_0_15 )
		{
			verboselog( *this, 1, "psx_sio0_r %s mode %04x\n", tag(), data & 0xffff );
		}
		if( ACCESSING_BITS_16_31 )
		{
			verboselog( *this, 1, "psx_sio0_r %s control %04x\n", tag(), data >> 16 );
		}
		break;
	case 3:
		data = m_baud << 16;
		if( ACCESSING_BITS_0_15 )
		{
			verboselog( *this, 0, "psx_sio0_r( %08x, %08x ) %08x\n", offset, mem_mask, data );
		}
		if( ACCESSING_BITS_16_31 )
		{
			verboselog( *this, 1, "psx_sio0_r %s baud %04x\n", tag(), data >> 16 );
		}
		break;
	default:
		data = 0;
		verboselog( *this, 0, "psx_sio0_r( %08x, %08x ) %08x\n", offset, mem_mask, data );
		break;
	}
	return data;
}

WRITE_LINE_MEMBER(psxsio0_device::write_rxd)
{
	m_rxd = state;
}

WRITE_LINE_MEMBER(psxsio0_device::write_dsr)
{
	if (state)
	{
		m_status &= ~SIO0_STATUS_DSR;
	}
	else if ((m_status & SIO0_STATUS_DSR) == 0)
	{
		m_status |= SIO0_STATUS_DSR;

		if( ( m_control & SIO0_CONTROL_DSR_IENA ) != 0 )
		{
			sio0_interrupt();
		}
	}
}
