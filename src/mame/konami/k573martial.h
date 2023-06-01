// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 Martial Beat I/O
 *
 */
#ifndef MAME_MACHINE_K573MARTIAL_H
#define MAME_MACHINE_K573MARTIAL_H

#pragma once

#include "diserial.h"
#include "bus/rs232/rs232.h"

#include <deque>

class k573martial_device : public device_t,
	public device_serial_interface,
	public device_rs232_port_interface
{
public:
	k573martial_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	virtual ioport_constructor device_input_ports() const override;

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
		SERIAL_RESP = 0xa5,
		NODE_REQ = 0x00,
		NODE_RESP = 0x01,
	};

	enum : uint8_t {
		CMD_INIT = 0x00,
		CMD_NODE_COUNT = 0x01,
		CMD_VERSION = 0x02,
		CMD_EXEC = 0x03,
	};

	enum : uint8_t {
		NODE_CMD_INIT = 0x00,
	};

	uint8_t calculate_crc8(std::deque<uint8_t>::iterator start, std::deque<uint8_t>::iterator end);

	emu_timer* m_timer_response;
	emu_timer* m_timer_io;

	std::deque<uint8_t> m_message;
	std::deque<uint8_t> m_response;

	required_ioport_array<6> m_inputs;
	uint8_t m_io_counter = 0;
	uint8_t m_io_state_sum = 0;
};

DECLARE_DEVICE_TYPE(KONAMI_573_MARTIAL_BEAT_IO, k573martial_device)

#endif // MAME_MACHINE_K573MARTIAL_H
