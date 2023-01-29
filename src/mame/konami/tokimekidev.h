// license:BSD-3-Clause
// copyright-holders:windyfairy
#ifndef MAME_MACHINE_TOKIMEKIDEV_H
#define MAME_MACHINE_TOKIMEKIDEV_H

#pragma once

#include "diserial.h"
#include "bus/rs232/rs232.h"

#include <deque>

class tokimekidev_device : public device_t,
	public device_serial_interface,
	public device_rs232_port_interface
{
public:
	tokimekidev_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	virtual ioport_constructor device_input_ports() const override;

	virtual WRITE_LINE_MEMBER(input_txd) override { device_serial_interface::rx_w(state); }

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
	static constexpr int BAUDRATE = 9600;

	emu_timer* m_timer_response;

	std::vector<uint8_t> m_message;
	std::deque<uint8_t> m_response;
};

DECLARE_DEVICE_TYPE(KONAMI_TOKIMEKI_DEV, tokimekidev_device)

#endif // MAME_MACHINE_TOKIMEKIDEV_H
