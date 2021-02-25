// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Firebeat Extend Board (Spectrum Analyzer for beatmania III)
 *
 */
#ifndef MAME_MACHINE_FIREBEATEXTEND_H
#define MAME_MACHINE_FIREBEATEXTEND_H

#pragma once

#include "machine/fdc37c665gt.h"
#include "machine/ins8250.h"

DECLARE_DEVICE_TYPE(KONAMI_FIREBEAT_EXTEND_SPECTRUM_ANALYZER, firebeat_extend_spectrum_analyzer_device)
DECLARE_DEVICE_TYPE(KONAMI_FIREBEAT_EXTEND, firebeat_extend_device)

class firebeat_extend_spectrum_analyzer_device : public device_t, public device_mixer_interface
{
public:
	firebeat_extend_spectrum_analyzer_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

    uint8_t read(offs_t offset);

protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;

	// device_sound_interface-level overrides
	void sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs) override;

private:
    enum {
        TOTAL_BUFFERS = 2,
        TOTAL_CHANNELS = 2,
        TOTAL_BARS = 6,

        FFT_LENGTH = 512
    };

	void update_fft();
	void apply_fft(uint32_t buf_index);

	float m_audio_buf[TOTAL_BUFFERS][TOTAL_CHANNELS][FFT_LENGTH];
	float m_fft_buf[TOTAL_CHANNELS][FFT_LENGTH];
	int m_audio_fill_index;
	int m_audio_count[TOTAL_CHANNELS];

    int m_bars[TOTAL_CHANNELS][TOTAL_BARS];
};

class firebeat_extend_device : public device_t
{
public:
	firebeat_extend_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	void extend_map(address_map &map);
	void extend_map_bm3(address_map &map);

protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_add_mconfig(machine_config &config) override;

private:
	uint8_t midi_uart_r(offs_t offset);
	void midi_uart_w(offs_t offset, uint8_t data);

	required_device<fdc37c665gt_device> m_fdc;
	required_device<pc16552_device> m_duart;
	required_device<firebeat_extend_spectrum_analyzer_device> m_spectrum_analyzer;
};

#endif // MAME_MACHINE_FIREBEATEXTEND_H
