// license:BSD-3-Clause
// copyright-holders:windyfairy
#include "emu.h"
#include "tokimekidev.h"

DEFINE_DEVICE_TYPE(KONAMI_TOKIMEKI_DEV, tokimekidev_device, "tokimekidev", "Konami Tokimeki Device")

tokimekidev_device::tokimekidev_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, KONAMI_TOKIMEKI_DEV, tag, owner, clock),
	device_serial_interface(mconfig, *this),
	device_rs232_port_interface(mconfig, *this),
	m_timer_response(nullptr)
{
}

void tokimekidev_device::device_start()
{
	int startbits = 1;
	int databits = 8;
	parity_t parity = PARITY_NONE;
	stop_bits_t stopbits = STOP_BITS_1;

	set_data_frame(startbits, databits, parity, stopbits);
	set_rate(BAUDRATE);

	m_timer_response = timer_alloc(FUNC(tokimekidev_device::send_response), this);
}

void tokimekidev_device::device_reset()
{
	m_timer_response->adjust(attotime::never);

	m_message.clear();
	m_response.clear();
}

void tokimekidev_device::tra_callback()
{
	output_rxd(transmit_register_get_data_bit());
}

void tokimekidev_device::tra_complete()
{
	m_timer_response->adjust(attotime::from_hz(BAUDRATE));
}

TIMER_CALLBACK_MEMBER(tokimekidev_device::send_response)
{
	if (!m_response.empty() && is_transmit_register_empty())
	{
		auto c = m_response.front();
		m_response.pop_front();
		transmit_register_setup(c);
	}
}

void tokimekidev_device::rcv_complete()
{
	receive_register_extract();

	auto c = get_received_char();
	// printf("byte: %02x\n", c);

	if (c == 0) {
		m_message.clear();
	} else if (c == 0x0a) {
		m_message.push_back(0);

		std::string cmd((char*)m_message.data());
		m_message.clear();

		// printf("cmd: %ld %s\n", cmd.size(), cmd.c_str());

		// for (int i = 0; i < m_message.size(); i++)
		// 	printf("c: %02x %c\n", m_message[i], m_message[i]);

		if (cmd == "S") {
			// Startup command
			m_response.push_back('E');
			m_response.push_back('N');
			m_response.push_back('\n');
		} else if (cmd.size() == 12) {
			for (int i = 0; i < 12; i++)
				m_response.push_back(m_message[i]);
			m_response.push_back('\n');
		}

		m_timer_response->adjust(attotime::from_hz(BAUDRATE));
	} else {
		m_message.push_back(c);
	}
}

INPUT_PORTS_START(tokimekidev)
INPUT_PORTS_END

ioport_constructor tokimekidev_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(tokimekidev);
}
