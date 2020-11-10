// license:BSD-3-Clause
// copyright-holders:windyfairy
#include "emu.h"
#include "speaker.h"

#include "k573fpga.h"


k573fpga_device::k573fpga_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, KONAMI_573_DIGITAL_FPGA, tag, owner, clock),
	mas3507d(*this, "mpeg"),
	use_ddrsbm_fpga(false)
{
}

void k573fpga_device::device_add_mconfig(machine_config &config)
{
	MAS3507D(config, mas3507d);
	mas3507d->sample_cb().set(*this, FUNC(k573fpga_device::get_decrypted));
	mas3507d->add_route(0, ":lspeaker", 1.0);
	mas3507d->add_route(1, ":rspeaker", 1.0);
}

void k573fpga_device::device_start()
{
}

void k573fpga_device::device_reset()
{
	mp3_cur_adr = 0;
	mp3_end_adr = 0;

	crypto_key1 = 0;
	crypto_key2 = 0;
	crypto_key3 = 0;

	is_stream_active = false;
	timer_was_reset = false;

	frame_count_since_last_update = 0;
	last_counter = previous_counter = 0;
}

const u32 frame_diff_count = 0 ; // The higher the number, the more the chart/visuals will be delayed
u32 frame_diff_idx = 0;
u32 last_playback_status = 0xb000;
void k573fpga_device::vblank_callback(int state)
{
	// TODO: Is it a better idea to have consistent timers between states instead of calculating diffs between get_counter() calls?
	if (state == 0) {
		frame_count_since_last_update++;
	}
}

bool k573fpga_device::is_streaming()
{
	return is_stream_active && mp3_cur_adr < mp3_end_adr;
}

void k573fpga_device::reset_counter() {
	timer_was_reset = true;
	frame_diff_idx = 0;

	if (is_mp3_playing()) {
		mas3507d->reg_write(0xaa, 1);
	}
}

void k573fpga_device::counter_update() {
	// DDR Extreme's sound options menu has logic like such:
	// 	If frame changed...
	// 		If counter is 0, mark song as ended
	// 		If counter is not 0, reset counter to 0
	//
	//	If the counter increments before the next frame occurs and the game can read
	// 	read the counter, the game never sees that the song ended.
	attotime ctr = machine().time();
	auto cur_playback_status = mas3507d->get_status();
	is_timer_active = (cur_playback_status == last_playback_status && last_playback_status > 0xb000) || cur_playback_status > last_playback_status;
	last_playback_status = cur_playback_status;

	if (timer_was_reset) {
		timer_was_reset = false;
		base_frame_counter = ctr;
		last_counter = previous_counter = 0;

		if (!is_stream_active && !is_mp3_playing()) {
			// There is another bug(?) (tested on real hardware) involving when the timer is stopped.
			// There seems to be a window of roughly about 5 frames of 44.1 KHz MP3 data on real hardware,
			// where the FPGA is no longer streaming data but the MAS3507D is still in the playing state.
			// If the counter is reset during that window the counter will reset to 0 and immediately continue
			// ticking up, and it won't ever stop ticking even after the MAS3507D goes into its idle state.
			//
			// As a way to emulate that window, the timer will only be completely stopped when both
			// the stream and MAS3507D are not in their respective active states when the counter is reset.
			is_timer_active = false;
		}
	}

	if (!is_timer_active) {
		last_counter = 0;
		return;
	}

	previous_counter = last_counter;

	auto counter_delta = (ctr - base_frame_counter).as_ticks(mas3507d->get_current_rate());
	if (frame_diff_idx < frame_diff_count) {
		logerror("Audio frame skip %d/%d... %d skipped\n", frame_diff_idx, frame_diff_count, counter_delta);
		frame_diff_idx++;
	} else {
		last_counter += counter_delta;
	}

	logerror("Counter updated @ %lf, diff = %d, counter = %08x\n", ctr.as_double(), counter_delta, last_counter);

	base_frame_counter = ctr;
}

u32 k573fpga_device::get_counter() {
	counter_update();
	return last_counter;
}

u32 k573fpga_device::get_counter_diff() {
	// On real hardware, this seems to reset the counter back to the previous frame's counter
	// as well as returns the difference from the last update.
	auto diff = last_counter - previous_counter;
	last_counter -= diff;
	previous_counter = last_counter;
	get_counter();
	return diff;
}

uint16_t k573fpga_device::mas_i2c_r()
{
	int scl = mas3507d->i2c_scl_r() << 13;
	int sda = mas3507d->i2c_sda_r() << 12;

	return scl | sda;
}

void k573fpga_device::mas_i2c_w(uint16_t data)
{
	mas3507d->i2c_scl_w(data & 0x2000);
	mas3507d->i2c_sda_w(data & 0x1000);
}

u16 k573fpga_device::get_mpeg_ctrl()
{
	// Audio playback status
	// 0x8000 = ?
	// 0xa000 = Error?
	// 0xb000 = Not playing
	// 0xc000 = Playing, demand pin = 0?
	// 0xd000 = Playing, demand pin = 1?
	return mas3507d->get_status();
}

bool k573fpga_device::is_mp3_playing()
{
	return get_mpeg_ctrl() > 0xb000;
}

u16 k573fpga_device::get_fpga_ctrl()
{
	// 0x0000 Not Streaming
	// 0x1000 Streaming
	return is_streaming() << 12;
}

void k573fpga_device::set_mpeg_ctrl(u16 data)
{
	logerror("FPGA MPEG control %c%c%c | %04x\n",
				data & 0x8000 ? '#' : '.',
				data & 0x4000 ? '#' : '.', // "Active" flag. Without this flag being set, the FPGA will never start streaming data
				data & 0x2000 ? '#' : '.',
				data);

	if (data == 0xa000) {
		// Mute
		mas3507d->reg_write(0xaa, 1);
		mas3507d->reset_playback();

		is_stream_active = false;
		reset_counter();
	} else if (data == 0xe000) {
		is_stream_active = true;
		reset_counter();

		// Unmute
		mas3507d->reg_write(0xaa, 0);
		mas3507d->reset_playback();
	}
}

u16 k573fpga_device::decrypt_default(u16 v)
{
	u16 m = crypto_key1 ^ crypto_key2;

	v = bitswap<16>(
		v,
		15 - BIT(m, 0xF),
		14 + BIT(m, 0xF),
		13 - BIT(m, 0xE),
		12 + BIT(m, 0xE),
		11 - BIT(m, 0xB),
		10 + BIT(m, 0xB),
		9 - BIT(m, 0x9),
		8 + BIT(m, 0x9),
		7 - BIT(m, 0x8),
		6 + BIT(m, 0x8),
		5 - BIT(m, 0x5),
		4 + BIT(m, 0x5),
		3 - BIT(m, 0x3),
		2 + BIT(m, 0x3),
		1 - BIT(m, 0x2),
		0 + BIT(m, 0x2)
	);

	v ^= (BIT(m, 0xD) << 14) ^
		(BIT(m, 0xC) << 12) ^
		(BIT(m, 0xA) << 10) ^
		(BIT(m, 0x7) << 8) ^
		(BIT(m, 0x6) << 6) ^
		(BIT(m, 0x4) << 4) ^
		(BIT(m, 0x1) << 2) ^
		(BIT(m, 0x0) << 0);

	v ^= bitswap<16>(
		(u16)crypto_key3,
		7, 0, 6, 1,
		5, 2, 4, 3,
		3, 4, 2, 5,
		1, 6, 0, 7
	);

	crypto_key1 = (crypto_key1 & 0x8000) | ((crypto_key1 << 1) & 0x7FFE) | ((crypto_key1 >> 14) & 1);

	if(((crypto_key1 >> 15) ^ crypto_key1) & 1)
		crypto_key2 = (crypto_key2 << 1) | (crypto_key2 >> 15);

	crypto_key3++;

	return v;
}

u16 k573fpga_device::decrypt_ddrsbm(u16 data)
{
	u8 key[16] = {0};
	u16 key_state = bitswap<16>(
		crypto_key1,
		13, 11, 9, 7,
		5, 3, 1, 15,
		14, 12, 10, 8,
		6, 4, 2, 0
	);

	for(int i = 0; i < 8; i++) {
		key[i * 2] = key_state & 0xff;
		key[i * 2 + 1] = (key_state >> 8) & 0xff;
		key_state = ((key_state & 0x8080) >> 7) | ((key_state & 0x7f7f) << 1);
	}

	u16 output_word = 0;
	for(int cur_bit = 0; cur_bit < 8; cur_bit++) {
		int even_bit_shift = cur_bit * 2;
		int odd_bit_shift = cur_bit * 2 + 1;
		bool is_even_bit_set = data & (1 << even_bit_shift);
		bool is_odd_bit_set = data & (1 << odd_bit_shift);
		bool is_key_bit_set = key[crypto_key3 & 15] & (1 << cur_bit);
		bool is_scramble_bit_set = key[(crypto_key3 - 1) & 15] & (1 << cur_bit);

		if(is_scramble_bit_set)
			std::swap(is_even_bit_set, is_odd_bit_set);

		if(is_even_bit_set ^ is_key_bit_set)
			output_word |= 1 << even_bit_shift;

		if(is_odd_bit_set)
			output_word |= 1 << odd_bit_shift;
	}

	crypto_key3++;

	return output_word;
}

u16 k573fpga_device::get_decrypted()
{
	if(!is_streaming()) {
		if (is_stream_active) {
			logerror("Reached end of audio! %d (%08x) %d (%04x)\n", get_counter(), get_counter(), mas3507d->get_frame_count(), mas3507d->get_frame_count());
		}

		is_stream_active = false;

		return 0;
	}

	u16 src = ram[mp3_cur_adr >> 1];
	u16 result = use_ddrsbm_fpga ? decrypt_ddrsbm(src) : decrypt_default(src);
	mp3_cur_adr += 2;

	return result;
}

DEFINE_DEVICE_TYPE(KONAMI_573_DIGITAL_FPGA, k573fpga_device, "k573fpga", "Konami 573 Digital I/O FPGA")
