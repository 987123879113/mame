// license:BSD-3-Clause
// copyright-holders:windyfairy
#ifndef MAME_MACHINE_K573FPGA_H
#define MAME_MACHINE_K573FPGA_H

#pragma once

#include "sound/mas3507d.h"
#include "machine/ds2401.h"

DECLARE_DEVICE_TYPE(KONAMI_573_DIGITAL_FPGA, k573fpga_device)

class k573fpga_device : public device_t
{
public:
	k573fpga_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	void set_ddrsbm_fpga(bool flag) { use_ddrsbm_fpga = flag; }

	void set_ram(u16 *v) { ram = v; }
	u16 get_decrypted();

	void set_crypto_key1(u16 v) { crypto_key1 = v; }
	void set_crypto_key2(u16 v) { crypto_key2 = v; }
	void set_crypto_key3(u8 v) { crypto_key3 = v; }

	uint32_t get_mp3_cur_adr() { return mp3_cur_adr; }
	void set_mp3_cur_adr(u32 v) { mp3_cur_adr = v; }

	uint32_t get_mp3_end_adr() { return mp3_end_adr; }
	void set_mp3_end_adr(u32 v) { mp3_end_adr = v; }

	uint16_t mas_i2c_r();
	void mas_i2c_w(uint16_t data);

	u16 get_fpga_ctrl();
	void set_mpeg_ctrl(u16 data);

	u16 get_mpeg_ctrl();

	u32 get_counter();
	u32 get_counter_diff();

	void counter_update();
	void reset_counter();
	void vblank_callback(int state);

protected:
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_add_mconfig(machine_config &config) override;

private:
	required_device<mas3507d_device> mas3507d;

	u16 *ram;

	u16 crypto_key1, crypto_key2;
	u8 crypto_key3;

	u32 mp3_cur_adr, mp3_end_adr;
	bool use_ddrsbm_fpga;

	bool is_stream_active, is_timer_active, timer_was_reset;
	u32 counter_base, counter_previous, counter_current, last_sample_rate;
	attotime counter_base_time, counter_previous_time;

	u16 decrypt_default(u16 data);
	u16 decrypt_ddrsbm(u16 data);

	bool is_mp3_playing();
	bool is_streaming();
};

#endif // MAME_MACHINE_K573FPGA_H
