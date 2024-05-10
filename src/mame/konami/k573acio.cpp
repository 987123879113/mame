// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami Serial Protocol
 *
 */
#include "emu.h"
#include "k573acio.h"

#define VERBOSE (LOG_GENERAL)
#define LOG_OUTPUT_FUNC osd_printf_info

#include "logmacro.h"

DEFINE_DEVICE_TYPE(KONAMI_573_ACIO_HOST, k573acio_host_device, "k573acio", "Konami 573 ACIO Host")

k573acio_host_device::k573acio_host_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, KONAMI_573_ACIO_HOST, tag, owner, clock),
	device_serial_interface(mconfig, *this),
	device_rs232_port_interface(mconfig, *this),
	m_timer_response(nullptr),
	m_nodecnt(0)
{
	m_node_device = nullptr;
}

void k573acio_host_device::device_start()
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

	m_timer_response = timer_alloc(FUNC(k573acio_host_device::send_response), this);

}

void k573acio_host_device::device_reset()
{
	m_timer_response->adjust(attotime::never);

	m_message.clear();
	m_response.clear();

	m_init_state = 0;
}

void k573acio_host_device::add_device(k573acio_node_device *dev)
{
	if (dev == nullptr)
		return;

	m_nodecnt++;

	dev->set_node_id(m_nodecnt);

	if(m_node_device)
		m_node_device->chain(dev);
	else
		m_node_device = dev;
}

void k573acio_host_device::tra_callback()
{
	output_rxd(transmit_register_get_data_bit());
}

void k573acio_host_device::tra_complete()
{
	m_timer_response->adjust(attotime::from_hz(BAUDRATE));
}

TIMER_CALLBACK_MEMBER(k573acio_host_device::send_response)
{
	if (!m_response.empty() && is_transmit_register_empty())
	{
		auto c = m_response.front();
		m_response.pop_front();
		transmit_register_setup(c);
	}
}

void k573acio_host_device::rcv_complete()
{
	receive_register_extract();

	m_message.push_back(get_received_char());

	while (!m_message.empty() && m_message.front() != HEADER_BYTE)
		m_message.pop_front();

	// A message must have at least a header byte, command, node ID, and sub command
	if (m_message.size() < 4)
		return;

	if (m_message[0] == 0xaa && m_message[1] == 0xaa && m_message[2] == 0xaa && m_message[3] == 0x55)
	{
		// Sync command
		for (int i = 0; i < 4; i++)
			m_message.pop_front();

		m_response.push_back(0xaa);
		m_response.push_back(0xaa);
		m_response.push_back(0xaa);
		m_response.push_back(0x55);

		m_init_state = 1;

	}
	else if (m_init_state == 1 && m_message[0] == 0xaa && m_message[1] == 0xaa && m_message[2] == 0x00 && m_message[3] == 0x00)
	{
		// Sync command 2
		for (int i = 0; i < 4; i++)
			m_message.pop_front();

		m_response.push_back(0xaa);
		m_response.push_back(0xaa);
		m_response.push_back(0x00);
		m_response.push_back(0x00);

		m_init_state = 2;
	}
	else if (m_init_state == 2 && m_message.size() >= 6)
	{
		auto cmd = m_message[1];
		auto node_id = m_message[2];
		auto subcmd = m_message[3];
		const int packet_len = m_message[4] ? (1 << (m_message[4] - 1)) + ((m_message[4] & 0xf0) ? 1 : 0) : 0;

		LOG("packet len %02x -> %02x\n", m_message[4], packet_len);

		if (m_message.size() >= packet_len + 6)
		{
			auto crc = calculate_crc8(m_message.begin() + 1, m_message.begin() + packet_len + 5);
			LOG("CRC: %02x vs %02x\n", m_message[packet_len + 5], crc);

			if (crc != m_message[packet_len + 5])
			{
				LOG("CRC mismatch!\n");

				for (int i = 0; i < packet_len + 6; i++)
					LOG("%02x ", m_message[i]);
				LOG("\n");

				for (int i = 0; i < packet_len + 6; i++)
					m_message.pop_front();
				return;
			}
		}
		else
		{
			return;
		}

		auto resplen = m_response.size();
		LOG("Command: ");
		for (int i = 0; i < m_message.size(); i++)
			LOG("%02x ", m_message[i]);
		LOG("\n");

		if (cmd == SERIAL_REQ) // host device
		{
			if (subcmd != CMD_NODE_COUNT && subcmd != CMD_VERSION && subcmd != CMD_EXEC)
			{
				LOG("Unknown command! %02x %02x\n", cmd, subcmd);
				return;
			}

			m_response.push_back(HEADER_BYTE);
			m_response.push_back(cmd == SERIAL_REQ ? SERIAL_RESP : NODE_RESP);
			m_response.push_back(node_id);
			m_response.push_back(subcmd);

			const auto payloadLengthIdx = m_response.size();
			m_response.push_back(0);

			if (subcmd != CMD_NODE_COUNT)
			{
				for (int i = 0; i < 6; i++)
					m_response.push_back(m_message[i]);
			}

			auto responseIdx = m_response.size() - payloadLengthIdx;

			if (subcmd == CMD_NODE_COUNT)
			{
				// ref: GF11 80090794

				m_response.push_back(m_nodecnt);
			}
			else if (subcmd == CMD_VERSION)
			{
				// TODO: Have node device generate this directly instead of doing it here

				// ref: GF11 80090988
				auto node = get_node_by_id(node_id);

				if (node != nullptr)
				{
					auto node_info = node->get_node_info();

					m_response.push_back(BIT(node_info->type, 0, 8));
					m_response.push_back(BIT(node_info->type, 8, 8));
					m_response.push_back(BIT(node_info->type, 16, 8));
					m_response.push_back(BIT(node_info->type, 24, 8));
					m_response.push_back(node_info->flag);
					m_response.push_back(node_info->major);
					m_response.push_back(node_info->minor);
					m_response.push_back(node_info->revision);

					for (int i = 0; i < std::size(node_info->product_name); i++)
						m_response.push_back(node_info->product_name[i]);
				}
				else
				{
					// TODO: how to handle empty node properly?
					for (int i = 0; i < 16; i++)
						m_response.push_back(0);
				}
			}
			else if (subcmd == CMD_EXEC)
			{
				m_response.push_back(0x00); // Status
			}

			for (int i = 0; i < packet_len + 6; i++)
				m_message.pop_front();

			auto size = m_response.size() - (payloadLengthIdx + 1);
			int bit = 0;
			while ((1 << bit) < (size & ~0x0f))
				bit++;

			m_response[payloadLengthIdx] = (bit + 1) | ((size - (1 << bit)) ? 0x10 : 0);

			m_response.push_back(calculate_crc8(m_response.begin() + responseIdx, m_response.end()));
		}
		else if (cmd == NODE_REQ)
		{
			std::deque<uint8_t> response, responsepost;

			m_node_device->message(node_id, m_message, response, responsepost);

			if (!response.empty())
			{
				for (int i = 0; i < 6 + packet_len; i++)
					m_response.push_back(m_message[i]);

				m_response.push_back(HEADER_BYTE);
				const auto responseIdx = m_response.size();
				m_response.push_back(cmd == SERIAL_REQ ? SERIAL_RESP : NODE_RESP);
				m_response.push_back(node_id);
				m_response.push_back(subcmd);

				auto size = response.size();
				int bit = 0;
				while ((1 << bit) < (size & ~0x0f))
					bit++;

				m_response.push_back((bit + 1) | ((size - (1 << bit)) ? 0x10 : 0));

				while (!response.empty())
				{
					m_response.push_back(response.front());
					response.pop_front();
				}

				m_response.push_back(calculate_crc8(m_response.begin() + responseIdx, m_response.end()));

				while (!responsepost.empty())
				{
					m_response.push_back(responsepost.front());
					responsepost.pop_front();
				}

				for (int i = 0; i < packet_len + 6; i++)
					m_message.pop_front();

				// if (m_response[payloadLengthIdx] > 0x7f)
			}
		}

		if (m_response.size() > resplen)
		{
			LOG("Response: ");
			for (int i = 0; i < m_response.size(); i++)
				LOG("%02x ", m_response[i]);
			LOG("\n\n");
		}
	}

	m_timer_response->adjust(attotime::from_hz(BAUDRATE));
}

k573acio_node_device *k573acio_host_device::get_node_by_id(uint32_t node_id)
{
	k573acio_node_device *node = m_node_device;

	while (node != nullptr)
	{
		if (node->get_node_id() == node_id)
			return node;

		node = node->next_node();
	}

	return nullptr;
}

uint8_t k573acio_host_device::calculate_crc8(std::deque<uint8_t>::iterator start, std::deque<uint8_t>::iterator end)
{
	return std::accumulate(start, end, 0) & 0xff;
}


////


k573acio_node_device::k573acio_node_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, type, tag, owner, clock)
{
	memset(&m_node_info, 0, sizeof(m_node_info));
	m_next_device = nullptr;
}

void k573acio_node_device::device_start()
{
}

void k573acio_node_device::device_reset()
{
}

void k573acio_node_device::chain(k573acio_node_device *dev)
{
	if(m_next_device)
		m_next_device->chain(dev);
	else
		m_next_device = dev;
}

void k573acio_node_device::message(uint32_t dest, std::deque<uint8_t> &message, std::deque<uint8_t> &response, std::deque<uint8_t> &responsepost)
{
	if (dest == m_node_id)
	{
		handle_message(message, response, responsepost);
		return;
	}

	if (m_next_device != nullptr)
		m_next_device->message(dest, message, response, responsepost);
}

void k573acio_node_device::handle_message(std::deque<uint8_t> &message, std::deque<uint8_t> &response, std::deque<uint8_t> &responsepost)
{
	[[maybe_unused]] const auto cmd = message[1];
	[[maybe_unused]] const auto node_id = message[2];
	const auto subcmd = message[3];

	if (subcmd == NODE_CMD_INIT)
	{
		response.push_back(0); // Status
	}
}
