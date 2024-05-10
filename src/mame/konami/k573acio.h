// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 Magnetic Card R/W Unit
 *
 */
#ifndef MAME_MACHINE_K573ACIO_H
#define MAME_MACHINE_K573ACIO_H

#pragma once

#include "diserial.h"
#include "bus/rs232/rs232.h"

#include <deque>
#include <unordered_map>

class k573acio_node_device;

class k573acio_host_device : public device_t,
	public device_serial_interface,
	public device_rs232_port_interface
{
public:
	k573acio_host_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	void add_device(k573acio_node_device *dev);

	virtual void input_txd(int state) override { device_serial_interface::rx_w(state); }

protected:
	virtual void device_start() override;
	virtual void device_reset() override;

	virtual void tra_callback() override;
	virtual void tra_complete() override;
	virtual void rcv_complete() override;

	TIMER_CALLBACK_MEMBER(send_response);
	TIMER_CALLBACK_MEMBER(send_io_packet);

private:
	static constexpr int TIMER_RESPONSE = 1;
	static constexpr int TIMER_IO = 2;
	static constexpr int BAUDRATE = 38400;

	const uint8_t HEADER_BYTE = 0xaa;

	enum : uint8_t {
		SERIAL_REQ = 0xaa,
		SERIAL_RESP = 0xaa,
		NODE_REQ = 0x00,
		NODE_RESP = 0x01,
	};

	enum : uint8_t {
		CMD_INIT = 0x00,
		CMD_NODE_COUNT = 0x01,
		CMD_VERSION = 0x02,
		CMD_EXEC = 0x03,
	};

	k573acio_node_device *get_node_by_id(uint32_t node_id);

	uint8_t calculate_crc8(std::deque<uint8_t>::iterator start, std::deque<uint8_t>::iterator end);

	emu_timer* m_timer_response;

	std::deque<uint8_t> m_message;
	std::deque<uint8_t> m_response;

	k573acio_node_device *m_node_device;

	uint32_t m_init_state;
	uint32_t m_nodecnt;
};

DECLARE_DEVICE_TYPE(KONAMI_573_ACIO_HOST, k573acio_host_device)


///

struct k573acio_node_info
{
	uint32_t type;
	uint8_t flag;
	uint8_t major;
	uint8_t minor;
	uint8_t revision;
	char product_name[8];
};

class k573acio_node_device : public device_t
{
public:
	void chain(k573acio_node_device *dev);

	uint32_t get_node_id() { return m_node_id; }
	k573acio_node_device *next_node() { return m_next_device; }

	const k573acio_node_info* get_node_info() { return &m_node_info; }

	void set_node_id(uint32_t node_id) { m_node_id = node_id; }

	void message(uint32_t dest, std::deque<uint8_t> &message, std::deque<uint8_t> &response, std::deque<uint8_t> &responsepost);

protected:
	k573acio_node_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);

	virtual void device_start() override;
	virtual void device_reset() override;

	virtual void handle_message(std::deque<uint8_t> &message, std::deque<uint8_t> &response, std::deque<uint8_t> &responsepost);

	uint32_t m_node_id;

	k573acio_node_device *m_next_device;
	k573acio_node_info m_node_info;

	enum : uint8_t {
		NODE_CMD_INIT = 0x00,
	};
};

#endif // MAME_MACHINE_K573ACIO_H
