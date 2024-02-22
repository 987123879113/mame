// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 e-Amusemental Rental Device
 *
 * Without this device's response, mamboagga gives an error stating "e-Amusement 2 transmission error".
 *
 * TODO: Should this even be a separate device?
 *
 */
#include "emu.h"
#include "k573rental.h"

DEFINE_DEVICE_TYPE(KONAMI_573_EAMUSE_RENTAL_DEVICE, k573rental_device, "k573rental", "Konami 573 e-Amusement Rental Device")

k573rental_device::k573rental_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, KONAMI_573_EAMUSE_RENTAL_DEVICE, tag, owner, clock),
	device_serial_interface(mconfig, *this),
	device_rs232_port_interface(mconfig, *this),
	m_timer_response(nullptr)
{
}

void k573rental_device::device_start()
{
	int startbits = 1;
	int databits = 8;
	parity_t parity = PARITY_NONE;
	stop_bits_t stopbits = STOP_BITS_1;

	set_data_frame(startbits, databits, parity, stopbits);
	set_rate(BAUDRATE);

	output_rxd(1);
	output_dcd(0);
	output_dsr(0);
	output_ri(0);
	output_cts(0);

	std::fill(std::begin(m_buffer), std::end(m_buffer), 0);
	m_response.clear();

	m_timer_response = timer_alloc(FUNC(k573rental_device::send_response), this);
}

void k573rental_device::device_reset()
{
	std::fill(std::begin(m_buffer), std::end(m_buffer), 0);
	m_response.clear();
}

void k573rental_device::tra_callback()
{
	output_rxd(transmit_register_get_data_bit());
}

void k573rental_device::tra_complete()
{
	m_timer_response->adjust(attotime::from_hz(BAUDRATE));
}

TIMER_CALLBACK_MEMBER(k573rental_device::send_response)
{
	if (!m_response.empty() && is_transmit_register_empty())
	{
		auto c = m_response.front();
		m_response.pop_front();
		transmit_register_setup(c);
	}
}

void k573rental_device::rcv_complete()
{
	receive_register_extract();

	for (int i = 0; i < sizeof(m_buffer) - 1; i++)
		m_buffer[i] = m_buffer[i + 1];

	m_buffer[sizeof(m_buffer) - 1] = get_received_char();

	if (m_buffer[0] == 0xa5 && m_buffer[1] == 0xc0) {
		for (int i = 0; i < 4; i++) {
			m_response.push_back(0xa5);
			m_response.push_back(0xc0);
		}

		m_timer_response->adjust(attotime::from_hz(BAUDRATE));
	}
}
