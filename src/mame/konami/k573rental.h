// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 e-Amusemental Rental Device
 *
 */
#ifndef MAME_MACHINE_K573RENTAL_H
#define MAME_MACHINE_K573RENTAL_H

#pragma once

#include "diserial.h"
#include "bus/rs232/rs232.h"

#include <deque>

class k573rental_device : public device_t,
	public device_serial_interface,
	public device_rs232_port_interface
{
public:
	k573rental_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	virtual void input_txd(int state) override { device_serial_interface::rx_w(state); }

protected:
	virtual void device_start() override;
	virtual void device_reset() override;

	virtual void tra_callback() override;
	virtual void tra_complete() override;
	virtual void rcv_complete() override;

	TIMER_CALLBACK_MEMBER(send_response);

private:
	static constexpr int TIMER_RESPONSE = 1;
	static constexpr int BAUDRATE = 19200;

	emu_timer *m_timer_response;
	uint8_t m_buffer[2];

	std::deque<uint8_t> m_response;
};

DECLARE_DEVICE_TYPE(KONAMI_573_EAMUSE_RENTAL_DEVICE, k573rental_device)

#endif // MAME_MACHINE_K573RENTAL_H
