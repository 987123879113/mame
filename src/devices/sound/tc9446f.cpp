// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
* Toshiba TC9446F, Audio Digital Processor for Decode of Dolby Digital (AC-3), MPEG2 Audio
*/

#include "emu.h"
#include "tc9446f.h"
#include "sound/mp3_audio.h"

#define LOG_GENERAL  (1 << 0)
#define VERBOSE      (LOG_GENERAL)
#define LOG_OUTPUT_STREAM std::cout

#include <iostream>
#include "logmacro.h"

DEFINE_DEVICE_TYPE(TC9446F, tc9446f_device, "tc9446f", "Toshiba TC9446F")

ALLOW_SAVE_TYPE(tc9446f_device::mode_select_t)

tc9446f_device::tc9446f_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, TC9446F, tag, owner, clock),
	device_sound_interface(mconfig, *this)
	, cb_mpeg_frame_sync(*this), cb_demand(*this)
	, stream(nullptr), stream_flags(STREAM_SYNCHRONOUS)
	, mp3data_count(0), sample_count(0), samples_idx(0)
{
}

void tc9446f_device::device_start()
{
	stream = stream_alloc(0, 2, 44100, stream_flags);

	mp3dec = std::make_unique<mp3_audio>(reinterpret_cast<const uint8_t *>(&mp3data[0]));

	save_item(NAME(mp3data));
	save_item(NAME(samples));
	save_item(NAME(m_mode_select));
	save_item(NAME(m_miack));
	save_item(NAME(m_indata));
	save_item(NAME(m_inbits));
	save_item(NAME(mp3data_count));
	save_item(NAME(sample_count));
	save_item(NAME(samples_idx));
	save_item(NAME(frame_channels));

	mp3dec->register_save(*this);
}

void tc9446f_device::device_reset()
{
	m_mode_select = SERIAL;
	m_miack = 0;
	m_indata = m_inbits = 0;
	m_cmd_target_addr = -1;
	m_cmd_word_count = -1;
	m_cmd_cur_word = 0;

	stream->set_sample_rate(44100);

	reset_playback();
}

int tc9446f_device::midio_r()
{
	// Serial: Data output
	// I2C: Data output (SDA)
	auto r = 0;
	//LOGMASKED(LOG_GENERAL, "midio_r: %d\n", r);
	return r;
}

int tc9446f_device::miack_r()
{
	// Serial: Acknowledge signal output and out of control detection output
	// I2C: Out of control detection output
	auto r = m_miack;
	m_miack = 0;
	//LOGMASKED(LOG_GENERAL, "miack_r: %d\n", r);
	return r;
}

void tc9446f_device::mimd_w(bool line)
{
	// Mode select input for MCU interface
	m_mode_select = line ? I2C : SERIAL;
	//LOGMASKED(LOG_GENERAL, "mimd_w: %d\n", line);
}

void tc9446f_device::mics_w(bool line)
{
	if (m_mode_select == I2C)
		return;

	m_miack = 0;
	//LOGMASKED(LOG_GENERAL, "mics_w: %d\n", line);
}

float tc9446f_device::gain_to_percentage(int val) {
	//  0 = 0x7f
	//  5 = 0x61
	// 10 = 0x49
	// 15 = 0x34
	// 20 = 0x25
	// 25 = 0x1a
	// 30 = 0x12
	if (val == 0x7f)
		return 0;

	// Not the real calculation because it's not written in public datasheets
	double db = round(20 * log10((127.0 - val) / 127.0));
	return powf(10.0, db / 20.0);
}

void tc9446f_device::midio_w(bool line)
{
	// Serial: Data input
	// I2C: Data input (SDA)
	m_indata = (m_indata << 1) | line;
	//LOGMASKED(LOG_GENERAL, "midio_w: %d | %d %06x\n", line, m_inbits, m_indata);
	m_inbits++;

	if (m_inbits >= 24) {
		if (m_cmd_word_count - m_cmd_cur_word < 0) {
			auto word_count = BIT(m_indata, 0, 4);
			//auto is_read = BIT(m_indata, 4);
			//auto is_soft_reset = BIT(m_indata, 5);
			//auto is_start_program_ram_boot = BIT(m_indata, 6);
			//auto is_incorrect_operation_detection_output = BIT(m_indata, 7);
			auto target_addr = BIT(m_indata, 8, 16);
			//printf("midio: %06x %04x %02x | %d %d %d %d\n", m_indata, target_addr, word_count, is_read, is_soft_reset, is_start_program_ram_boot, is_incorrect_operation_detection_output);

			m_cmd_target_addr = target_addr;
			m_cmd_word_count = word_count;
			m_cmd_cur_word = 0;
		}
		else {
			if (m_cmd_target_addr == 0x23d1 && m_cmd_cur_word == 0) {
				// Volume write
				auto gain = gain_to_percentage(m_indata);
				//printf("Changing volume to %02x %lf\n", m_indata, gain);

				set_output_gain(0, gain);
				set_output_gain(1, gain);
			}

			m_cmd_cur_word++;
		}

		m_miack = 1;
		m_indata = 0;
		m_inbits = 0;
	}
}

void tc9446f_device::mick_w(bool line)
{
	// Serial: Clock input
	// I2C: Clock input (SCL)
	//LOGMASKED(LOG_GENERAL, "mick_w: %d\n", line);
}

void tc9446f_device::audio_w(uint8_t byte)
{
	if (mp3data_count >= mp3data.size()) {
		std::copy(mp3data.begin() + 1, mp3data.end(), mp3data.begin());
		mp3data_count--;
	}

	mp3data[mp3data_count++] = byte;

	cb_demand(mp3data_count < mp3data.size());
}

void tc9446f_device::fill_buffer()
{
	cb_mpeg_frame_sync(0);

	int pos = 0, frame_sample_rate = 0;
	bool decoded_frame = mp3dec->decode_buffer(pos, mp3data_count, &samples[0], sample_count, frame_sample_rate, frame_channels);
	samples_idx = 0;

	if (!decoded_frame || sample_count == 0) {
		// Frame decode failed
		if (mp3data_count >= mp3data.size()) {
			std::copy(mp3data.begin() + 1, mp3data.end(), mp3data.begin());
			mp3data_count--;
		}

		cb_demand(mp3data_count < mp3data.size());
		return;
	}

	std::copy(mp3data.begin() + pos, mp3data.end(), mp3data.begin());
	mp3data_count -= pos;

	stream->set_sample_rate(frame_sample_rate);

	cb_mpeg_frame_sync(1);

	cb_demand(mp3data_count < mp3data.size());
}

void tc9446f_device::append_buffer(std::vector<write_stream_view>& outputs, int& pos, int scount)
{
	int s1 = scount - pos;
	int bytes_per_sample = std::min(frame_channels, 2); // More than 2 channels is unsupported here

	if (s1 > sample_count)
		s1 = sample_count;

	for (int i = 0; i < s1; i++) {
		outputs[0].put_int(pos, samples[samples_idx * bytes_per_sample], 32768);
		outputs[1].put_int(pos, samples[samples_idx * bytes_per_sample + (bytes_per_sample >> 1)], 32768);

		samples_idx++;
		pos++;

		if (samples_idx >= sample_count) {
			sample_count = 0;
			return;
		}
	}
}

void tc9446f_device::reset_playback()
{
	std::fill(mp3data.begin(), mp3data.end(), 0);
	std::fill(samples.begin(), samples.end(), 0);

	mp3dec->clear();
	mp3data_count = 0;
	sample_count = 0;
	samples_idx = 0;
	frame_channels = 2;

	cb_demand(mp3data_count < mp3data.size());
}

void tc9446f_device::sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs)
{
	int csamples = outputs[0].samples();
	int pos = 0;

	while (pos < csamples) {
		if (sample_count == 0)
			fill_buffer();

		if (sample_count <= 0) {
			outputs[0].fill(0, pos);
			outputs[1].fill(0, pos);
			return;
		}

		append_buffer(outputs, pos, csamples);
	}
}
