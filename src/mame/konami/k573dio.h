// license:BSD-3-Clause
// copyright-holders:smf, windyfairy
#ifndef MAME_KONAMI_K573DIO_H
#define MAME_KONAMI_K573DIO_H

#pragma once

#include <deque>

#include "imagedev/bitbngr.h"
#include "machine/ds2401.h"
#include "machine/timer.h"
#include "sound/mas3507d.h"

class k573dio_device : public device_t
{
public:
	k573dio_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	auto output_callback() { return output_cb.bind(); }

	void set_ddrsbm_fpga(bool flag) { m_is_ddrsbm_fpga = flag; }

	void amap(address_map &map);

	void explus_speed_normal();
	void explus_speed_inc1();
	void explus_speed_inc2();

protected:
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual const tiny_rom_entry *device_rom_region() const override;
	virtual void device_add_mconfig(machine_config &config) override;

private:
	static const int NETWORK_CONNECTIONS = 3;

	void reset_fpga_state();

	void explus_set_clock(uint32_t speed);

	template <uint16_t Val> uint16_t dummy_r(offs_t offset);

	uint16_t fpga_status_r();
	void fpga_status_w(uint16_t data);

	void fpga_firmware_w(uint16_t data);

	uint16_t digital_id_r();
	void digital_id_w(uint16_t data);

	uint16_t mas_i2c_r();
	void mas_i2c_w(uint16_t data);

	void ram_write_adr_high_w(uint16_t data);
	void ram_write_adr_low_w(uint16_t data);

	void ram_read_adr_high_w(uint16_t data);
	void ram_read_adr_low_w(uint16_t data);

	uint16_t ram_get_next_value();
	uint16_t ram_peek_r();
	uint16_t ram_r();
	void ram_w(uint16_t data);

	void mpeg_current_adr_high_w(uint16_t data);
	void mpeg_current_adr_low_w(uint16_t data);

	void mpeg_end_adr_high_w(uint16_t data);
	void mpeg_end_adr_low_w(uint16_t data);

	uint16_t mpeg_current_adr_high_r();
	uint16_t mpeg_current_adr_low_r();

	void mpeg_key_1_w(uint16_t data);
	void mpeg_key_2_w(uint16_t data);
	void mpeg_key_3_w(uint16_t data);

	uint16_t mpeg_frame_counter_r();

	uint16_t mpeg_status_r();

	uint16_t mpeg_ctrl_r();
	void mpeg_ctrl_w(uint16_t data);

	uint16_t mpeg_timer_diff_r();
	uint16_t mpeg_timer_high_r();
	uint16_t mpeg_timer_low_r();
	void mpeg_timer_low_w(uint16_t data);

	TIMER_CALLBACK_MEMBER(mpeg_data_transfer);
	TIMER_CALLBACK_MEMBER(mpeg_frame_timeout);
	uint32_t mpeg_get_current_timer();
	void mpeg_frame_sync(int state);
	void mpeg_demand(int state);
	void set_mpeg_sampling_frequency(uint32_t freq);

	uint16_t decrypt_default(uint16_t data);
	uint16_t decrypt_ddrsbm(uint16_t data);

	template <int Offset> void output_w(uint16_t data);

	TIMER_DEVICE_CALLBACK_MEMBER(network_update_callback);
	uint16_t network_r();
	void network_w(uint16_t data);

	uint16_t network_output_buf_size_r();
	uint16_t network_input_buf_size_r();

	void network_id_w(uint16_t data);

	memory_share_creator<uint16_t> m_ram;
	required_device<ds2401_device> m_digital_id;
	required_device<mas3507d_device> m_mas3507d;
	devcb_write8 output_cb;

	required_device_array<bitbanger_device, NETWORK_CONNECTIONS> m_network;

	bool m_is_ddrsbm_fpga;

	uint8_t m_output_data[8];

	uint32_t m_ram_addr;
	uint16_t m_last_valid_ram_read;

	uint16_t m_fpga_status;
	bool m_is_fpga_initialized;
	uint32_t m_firmware_bits_received;
	bool m_fpga_pre_init;

	uint32_t m_mpeg_timer, m_mpeg_timer_base;
	uint32_t m_mpeg_current_addr, m_mpeg_end_addr;

	uint16_t m_crypto_key1, m_crypto_key2;
	uint8_t m_crypto_key3;

	uint16_t m_mpeg_ctrl;
	uint16_t m_mpeg_status;
	uint16_t m_mpeg_frame_counter;
	bool m_mpeg_current_has_ended;
	bool m_mpeg_timer_enabled;

	uint16_t m_mp3_remaining_bytes, m_mp3_data;

	uint32_t m_mpeg_timer_frequency, m_mpeg_timer_frequency_div;
	attotime m_mpeg_timer_last_update;

	uint16_t m_digital_id_cached;

	uint16_t m_network_id;
	size_t m_network_buffer_output_waiting_size;
	std::deque<uint8_t> m_network_buffer_muxed; // TODO: Rewrite to use ring buffers, initialize
	std::deque<uint8_t> m_network_buffer_output; // TODO: Rewrite to use ring buffers, initialize
	std::deque<uint8_t> m_network_buffer_input[NETWORK_CONNECTIONS]; // TODO: Rewrite to use ring buffers, initialize
	std::deque<std::deque<uint8_t>> m_network_buffer_output_queue; // TODO: Rewrite to use ring buffers, initialize

	emu_timer* m_stream_timer;
	emu_timer* m_mpeg_frame_timer;
};

DECLARE_DEVICE_TYPE(KONAMI_573_DIGITAL_IO_BOARD, k573dio_device)

#endif // MAME_KONAMI_K573DIO_H
