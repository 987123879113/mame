// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 Martial Beat I/O
 *
 * Sys573 GFDM's magnetic card readers are also the same protocol, except the connected nodes are ICCA card readers.
 *
 * TODO: Refactor so this code is not tied to Martial Beat and the nodes can be attached dynamically.
 *
 */
#include "emu.h"
#include "k573martial.h"

DEFINE_DEVICE_TYPE(KONAMI_573_MARTIAL_BEAT_IO, k573martial_device, "k573martial", "Konami 573 Martial Beat I/O")

k573martial_device::k573martial_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, KONAMI_573_MARTIAL_BEAT_IO, tag, owner, clock),
	device_serial_interface(mconfig, *this),
	device_rs232_port_interface(mconfig, *this),
	m_timer_response(nullptr),
	m_inputs(*this, "IN%u", 1)
{
}

void k573martial_device::device_start()
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

	m_message.clear();
	m_response.clear();
	m_io_counter = 0;
	m_io_state_sum = 0;

	m_timer_response = timer_alloc(FUNC(k573martial_device::send_response), this);
	m_timer_io = timer_alloc(FUNC(k573martial_device::send_io_packet), this);

}

void k573martial_device::device_reset()
{
	m_timer_response->adjust(attotime::never);
	m_timer_io->adjust(attotime::never);

	m_message.clear();
	m_response.clear();
	m_io_counter = 0;
	m_io_state_sum = 0;
}

void k573martial_device::tra_callback()
{
	output_rxd(transmit_register_get_data_bit());
}

void k573martial_device::tra_complete()
{
	m_timer_response->adjust(attotime::from_hz(BAUDRATE));
}

TIMER_CALLBACK_MEMBER(k573martial_device::send_response)
{
	if (!m_response.empty() && is_transmit_register_empty())
	{
		auto c = m_response.front();
		m_response.pop_front();
		transmit_register_setup(c);
	}
}

TIMER_CALLBACK_MEMBER(k573martial_device::send_io_packet)
{
	if (is_transmit_register_empty()) {
		if (m_io_counter >= 6) {
			transmit_register_setup(m_io_state_sum);
			m_io_counter = 0;
			m_io_state_sum = 0;
		}
		else {
			auto c = m_inputs[m_io_counter]->read();
			m_io_state_sum = (m_io_state_sum + (c & 0x7f)) & 0x7f;
			transmit_register_setup(c);
			m_io_counter++;
		}
	}

	m_timer_io->adjust(attotime::from_hz(BAUDRATE));
}

void k573martial_device::rcv_complete()
{
	receive_register_extract();

	m_message.push_back(get_received_char());

	while (m_message.front() != HEADER_BYTE) {
		m_message.pop_front();
	}

	// A message must have at least a header byte, command, node ID, and sub command
	if (m_message.size() < 4)
		return;

	if (m_message[0] == 0xaa && m_message[1] == 0xaa && m_message[2] == 0xaa && m_message[3] == 0x55) {
		// ref: 8002d3d0
		// Sync command
		for (int i = 0; i < 4; i++)
			m_message.pop_front();

		m_response.push_back(0xaa);
		m_response.push_back(0xaa);
		m_response.push_back(0xaa);
		m_response.push_back(0x55);

	}
	else {
		auto cmd = m_message[1];
		auto node_id = m_message[2];
		auto subcmd = m_message[3];

		if (cmd == SERIAL_REQ && subcmd == CMD_INIT) {
			// ref: 8002d478
			for (int i = 0; i < 4; i++) {
				m_response.push_back(m_message.front());
				m_message.pop_front();
			}
		}
		else {
			size_t packet_len = m_message[4];

			if (m_message.size() >= packet_len + 6) {
				auto crc = calculate_crc8(m_message.begin() + 1, m_message.begin() + packet_len + 5);

				if (crc != m_message[packet_len + 5]) {
					printf("CRC mismatch!\n");
					for (int i = 0; i < packet_len + 6; i++) {
						m_message.pop_front();
					}
					return;
				}

				if ((cmd == SERIAL_REQ && (subcmd != CMD_NODE_COUNT && subcmd != CMD_VERSION && subcmd != CMD_EXEC))
					|| (cmd == NODE_REQ && (subcmd != CMD_INIT))) {
					printf("Unknown command! %02x %02x\n", cmd, subcmd);
					return;
				}

				// Put the full request at the beginning of the response
				for (int i = 0; i < packet_len + 6; i++) {
					m_response.push_back(m_message.front());
					m_message.pop_front();
				}

				auto responseIdx = m_response.size();
				m_response.push_back(HEADER_BYTE);
				m_response.push_back(cmd == SERIAL_REQ ? SERIAL_RESP : NODE_RESP);
				m_response.push_back(node_id);
				m_response.push_back(subcmd);

				uint8_t payloadLength = 0;
				auto payloadLengthIdx = m_response.size();
				m_response.push_back(payloadLength);

				if (cmd == SERIAL_REQ && subcmd == CMD_NODE_COUNT) {
					// ref: 8002d01c
					payloadLength = 1;
					m_response.push_back(1); // 1 node connected for Martial Beat I/O
				}
				else if (cmd == SERIAL_REQ && subcmd == CMD_VERSION) {
					// ref: 8002d14c
					// This message must be 0x16 bytes total (including prepended packet and checksums), but the payload length is supposed to be 5
					payloadLength = 5;

					// I/O unit type
					m_response.push_back(0x00);
					m_response.push_back(0x00);
					m_response.push_back(0x03);
					m_response.push_back(0x01);

					// Unused padding
					m_response.push_back(0x00);

					// I/O unit version
					m_response.push_back(0x01);
					m_response.push_back(0x00);
					m_response.push_back(0x00);

					// Padding
					m_response.push_back(0x00);
					m_response.push_back(0x00);
					m_response.push_back(0x00);
				}
				else if (cmd == SERIAL_REQ && subcmd == CMD_EXEC) {
					// ref: 8002d520
					payloadLength = 1;
					m_response.push_back(0x00); // Status
				}
				else if (cmd == NODE_REQ && subcmd == NODE_CMD_INIT) {
					payloadLength = 1;
					m_response.push_back(0x00); // Status

					m_timer_io->adjust(attotime::from_hz(BAUDRATE));
				}

				m_response[payloadLengthIdx] = payloadLength;
				m_response.push_back(calculate_crc8(m_response.begin() + responseIdx + 1, m_response.end()));
			}
		}
	}

	m_timer_response->adjust(attotime::from_hz(BAUDRATE));
}

uint8_t k573martial_device::calculate_crc8(std::deque<uint8_t>::iterator start, std::deque<uint8_t>::iterator end)
{
	return std::accumulate(start, end, 0) & 0xff;
}

INPUT_PORTS_START(k573martial)
	PORT_START("IN1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_NAME("Top Left, Top 1")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_NAME("Top Left, Top 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON4) PORT_NAME("Top Right, Top 1")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON4) PORT_NAME("Top Right, Top 2")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON2) PORT_NAME("Top Left, Middle 1")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON5) PORT_NAME("Top Right, Middle 4")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_BUTTON3) PORT_NAME("Top Left, Bottom 1")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED) // Needs to be low for controls to work

	PORT_START("IN2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON2) PORT_NAME("Top Left, Middle 4")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON5) PORT_NAME("Top Right, Middle 1")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON5) PORT_NAME("Top Right, Middle 2")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON5) PORT_NAME("Top Right, Middle 3")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON6) PORT_NAME("Top Right, Bottom 4")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON7) PORT_NAME("Left Punch 7")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_BUTTON9) PORT_NAME("Right Punch 7")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("IN3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON3) PORT_NAME("Top Left, Bottom 3")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON3) PORT_NAME("Top Left, Bottom 4")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON6) PORT_NAME("Top Right, Bottom 1")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON6) PORT_NAME("Top Right, Bottom 2")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON6) PORT_NAME("Top Right, Bottom 3")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON2) PORT_NAME("Top Left, Middle 2")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_BUTTON2) PORT_NAME("Top Left, Middle 3")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("IN4")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON9) PORT_NAME("Right Punch 2")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON9) PORT_NAME("Right Punch 1")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON10) PORT_NAME("Right Kick 4")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON10) PORT_NAME("Right Kick 3")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON10) PORT_NAME("Right Kick 2")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON10) PORT_NAME("Right Kick 1")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_BUTTON3) PORT_NAME("Top Left, Bottom 2")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("IN5")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON8) PORT_NAME("Left Kick 3")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON8) PORT_NAME("Left Kick 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON8) PORT_NAME("Left Kick 1")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON9) PORT_NAME("Right Punch 6")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON9) PORT_NAME("Right Punch 5")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON9) PORT_NAME("Right Punch 4")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_BUTTON9) PORT_NAME("Right Punch 3")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("IN6")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON7) PORT_NAME("Left Punch 6")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON7) PORT_NAME("Left Punch 5")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON7) PORT_NAME("Left Punch 4")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON7) PORT_NAME("Left Punch 3")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON7) PORT_NAME("Left Punch 2")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON7) PORT_NAME("Left Punch 1")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_BUTTON8) PORT_NAME("Left Kick 4")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

ioport_constructor k573martial_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(k573martial);
}
