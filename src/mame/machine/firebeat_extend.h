// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Firebeat Extend Board (Spectrum Analyzer for beatmania III)
 *
 */
#ifndef MAME_MACHINE_FIREBEATEXTEND_H
#define MAME_MACHINE_FIREBEATEXTEND_H

#pragma once

DECLARE_DEVICE_TYPE(KONAMI_FIREBEAT_EXTEND_SPECTRUM_ANALYZER, firebeat_extend_spectrum_analyzer_device)

class firebeat_extend_spectrum_analyzer_device : public device_t, public device_mixer_interface
{
public:
	firebeat_extend_spectrum_analyzer_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

    int get_bar_value(int channel, int bar);

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

#endif // MAME_MACHINE_FIREBEATEXTEND_H
