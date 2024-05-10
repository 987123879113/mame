// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 Magnetic Card R/W Unit
 *
 * TODO: Force the card to always be ejected on reset (maybe don't use MAME's toggle setting?)
 * TODO: You shouldn't be able to eject the card while it's in certain states
 *
 */
#include "emu.h"
#include "k573cardunit.h"

#include "formats/imageutl.h"


// #define VERBOSE (LOG_GENERAL)
#define LOG_OUTPUT_FUNC osd_printf_info

#include "logmacro.h"


enum : uint8_t {
	NODE_CMD_INIT = 0x00,
	NODE_CMD_INIT2 = 0x01, // Called after NODE_CMD_INIT in some cases

	NODE_CMD_CARD_INIT = 0x10,
	NODE_CMD_CARD_INIT2 = 0x11, // Called after NODE_CMD_CARD_INIT in some cases
	NODE_CMD_CARD_GET_STATUS = 0x12,
	NODE_CMD_CARD_CTRL = 0x14,
	NODE_CMD_CARD_CTRL2 = 0x15, // Called after NODE_CMD_CARD_CTRL in some cases
	NODE_CMD_CARD_WRITE = 0x16,
	NODE_CMD_CARD_READ = 0x18,
	NODE_CMD_CARD_FORMAT = 0x1e,
	NODE_CMD_CARD_FORMAT2 = 0x1f, // Called after NODE_CMD_CARD_FORMAT in some cases

	NODE_CMD_KEYBOARD_INIT = 0x20,
	// ? = 0x22
	NODE_CMD_KEYBOARD_GET_STATUS = 0x24,
	NODE_CMD_KEYBOARD_READ_DATA = 0x26,
	NODE_CMD_KEYBOARD_GET_SIZE = 0x27,
};

enum : uint8_t {
	CARD_SLOT_STATE_CLOSE = 0,
	CARD_SLOT_STATE_OPEN = 1,
	CARD_SLOT_STATE_EJECT = 2,
	CARD_SLOT_STATE_FORMAT = 3,
	CARD_SLOT_STATE_READ = 4,
	CARD_SLOT_STATE_WRITE = 5,
};

DEFINE_DEVICE_TYPE(KONAMI_573_MAGNETIC_CARD_READER, k573cardunit_device, "k573cardunit", "Konami 573 Magnetic Card R/W Unit")

k573cardunit_device::k573cardunit_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: k573acio_node_device(mconfig, KONAMI_573_MAGNETIC_CARD_READER, tag, owner, clock)
	, device_memcard_image_interface(mconfig, *this)
	, m_keypad(*this, "KEYPAD")
{
	std::fill(std::begin(m_card_data), std::end(m_card_data), 0);
	m_card_inserted = false;

	m_node_info.type = 3;
	m_node_info.major = 1;
	m_node_info.minor = 6;
	memcpy(m_node_info.product_name, "ICCA", 4);
}

void k573cardunit_device::device_start()
{
	k573acio_node_device::device_start();
}

void k573cardunit_device::device_reset()
{
	k573acio_node_device::device_reset();

	m_last_input = 0;
}

void k573cardunit_device::device_config_complete()
{
	add_format("mag", "Magnetic Card Image", "bin", "");
}

INPUT_CHANGED_MEMBER(k573cardunit_device::card_media_update)
{
	m_card_inserted = m_keypad->read() & 0x1000;
}

void k573cardunit_device::handle_message(std::deque<uint8_t> &message, std::deque<uint8_t> &response, std::deque<uint8_t> &responsepost)
{
	const auto cmd = message[3];
	const int packet_len = message[4] ? (1 << (message[4] - 1)) + ((message[4] & 0xf0) ? 1 : 0) : 0;

	if (cmd == NODE_CMD_INIT
		|| cmd == NODE_CMD_CARD_INIT
		|| cmd == NODE_CMD_CARD_INIT2
		|| cmd == NODE_CMD_KEYBOARD_INIT
		|| cmd == NODE_CMD_KEYBOARD_GET_STATUS
		|| cmd == NODE_CMD_CARD_FORMAT
		|| cmd == NODE_CMD_CARD_FORMAT2)
	{
		response.push_back(0);
	}
	else if (cmd == NODE_CMD_CARD_GET_STATUS)
	{
		int state = 0;

		if (m_card_inserted)
		{
			state |= 2;
			state |= 64; // Front sensor
			state |= 128; // Back sensor
		}

		response.push_back(state);
	}
	else if (cmd == NODE_CMD_CARD_CTRL || cmd == NODE_CMD_CARD_CTRL2)
	{
		const auto new_card_slot_state = message[5];
		printf("card slot state: %d\n", new_card_slot_state);
		response.push_back(0);
	}
	else if (cmd == NODE_CMD_CARD_WRITE)
	{
		std::copy(std::begin(message) + 5, std::begin(message) + std::min<int>(std::size(m_card_data), packet_len), std::begin(m_card_data));
		response.push_back(0);
	}
	else if (cmd == NODE_CMD_CARD_READ)
	{
		if (!is_open() || !m_card_inserted)
		{
			response.push_back(0xff);
		}
		else
		{
			response.push_back(0);

			for (int i = 0; i < std::size(m_card_data); i++)
				response.push_back(m_card_data[i]);
		}
	}
	else if (cmd == NODE_CMD_KEYBOARD_READ_DATA)
	{
		const int padlen = message[5] ? (1 << (message[5] - 1)) + ((message[5] & 0xf0) ? 1 : 0) : 0; // is this right?
		const auto input = m_keypad->read();
		uint32_t found = 0;

		constexpr uint8_t keypad_vals[] = {
			0x69, // 1
			0x72, // 2
			0x7a, // 3
			0x6b, // 4
			0x73, // 5
			0x74, // 6
			0x6c, // 7
			0x75, // 8
			0x7d, // 9
			0x70, // 0
			0x70, // 000?
			0x66, // ?
		};

		const auto resplen = response.size();

		for (int i = 0; i < std::size(keypad_vals) && i < padlen; i++)
		{
			if (!BIT(found, i) && BIT(input, i))
			{
				if (!BIT(m_last_input, i))
					response.push_back(keypad_vals[i]);
				found |= 1 << i;
			}
		}

		if (response.size() == resplen)
			response.push_back(0);

		m_last_input = found;
	}
	else if (cmd == NODE_CMD_KEYBOARD_GET_SIZE)
	{
		// TODO
		response.push_back(0);
	}
	else
		k573acio_node_device::handle_message(message, response, responsepost);
}

std::pair<std::error_condition, std::string> k573cardunit_device::call_load()
{
	if (is_open())
	{
		fread(m_card_data, std::size(m_card_data));
	}

	return std::make_pair(std::error_condition(), std::string());
}

std::pair<std::error_condition, std::string> k573cardunit_device::call_create(int format_type, util::option_resolution *format_options)
{
	uint8_t header[] = {0x08, 0x1f, 0x7d, 0xf0, 0x56};

	std::fill(std::begin(m_card_data), std::end(m_card_data), 0);
	std::copy(std::begin(header), std::end(header), std::begin(m_card_data));

	const auto ret = fwrite(m_card_data, std::size(m_card_data));
	if(ret != std::size(m_card_data))
		return std::make_pair(image_error::UNSPECIFIED, std::string());

	return std::make_pair(std::error_condition(), std::string());
}

void k573cardunit_device::call_unload()
{
	fseek(0, SEEK_SET);
	fwrite(m_card_data, std::size(m_card_data));
}

INPUT_PORTS_START( k573cardunit_controls )
	PORT_START("KEYPAD")
	PORT_BIT( 0x00000001, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_NAME("Keypad 1")
	PORT_BIT( 0x00000002, IP_ACTIVE_HIGH, IPT_BUTTON2) PORT_NAME("Keypad 2")
	PORT_BIT( 0x00000004, IP_ACTIVE_HIGH, IPT_BUTTON3) PORT_NAME("Keypad 3")
	PORT_BIT( 0x00000008, IP_ACTIVE_HIGH, IPT_BUTTON4) PORT_NAME("Keypad 4")
	PORT_BIT( 0x00000010, IP_ACTIVE_HIGH, IPT_BUTTON5) PORT_NAME("Keypad 5")
	PORT_BIT( 0x00000020, IP_ACTIVE_HIGH, IPT_BUTTON6) PORT_NAME("Keypad 6")
	PORT_BIT( 0x00000040, IP_ACTIVE_HIGH, IPT_BUTTON7) PORT_NAME("Keypad 7")
	PORT_BIT( 0x00000080, IP_ACTIVE_HIGH, IPT_BUTTON8) PORT_NAME("Keypad 8")
	PORT_BIT( 0x00000100, IP_ACTIVE_HIGH, IPT_BUTTON9) PORT_NAME("Keypad 9")
	PORT_BIT( 0x00000200, IP_ACTIVE_HIGH, IPT_BUTTON10) PORT_NAME("Keypad 0")
	PORT_BIT( 0x00000400, IP_ACTIVE_HIGH, IPT_BUTTON11) PORT_NAME("Keypad 000")
	PORT_BIT( 0x00000800, IP_ACTIVE_HIGH, IPT_BUTTON11) PORT_NAME("Keypad Unk")
	PORT_BIT( 0x00001000, IP_ACTIVE_HIGH, IPT_BUTTON12) PORT_TOGGLE PORT_NAME("Insert/Eject Card") PORT_CHANGED_MEMBER(DEVICE_SELF, k573cardunit_device, card_media_update, 0)
INPUT_PORTS_END

ioport_constructor k573cardunit_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(k573cardunit_controls);
}
