// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Firebeat Extend Board (Spectrum Analyzer for beatmania III)
 * ref: https://forum.cockos.com/showthread.php?t=231070
 */

#include "emu.h"

#include "machine/firebeat_extend.h"

#include "wdlfft/fft.h"

#include <cmath>

firebeat_extend_spectrum_analyzer_device::firebeat_extend_spectrum_analyzer_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, KONAMI_FIREBEAT_EXTEND_SPECTRUM_ANALYZER, tag, owner, clock),
    device_mixer_interface(mconfig, *this, 2)
{
}

void firebeat_extend_spectrum_analyzer_device::device_start()
{
	WDL_fft_init();
}

void firebeat_extend_spectrum_analyzer_device::device_reset()
{
	for (int ch = 0; ch < TOTAL_CHANNELS; ch++)
	{
		for (int i = 0; i < TOTAL_BUFFERS; i++) {
			memset(m_audio_buf[i][ch], 0, sizeof(float) * FFT_LENGTH);
		}

		memset(m_fft_buf[ch], 0, sizeof(float) * FFT_LENGTH);

		m_audio_count[ch] = 0;

        for (int i = 0; i < TOTAL_BARS; i++)
        {
            m_bars[ch][i] = 0;
        }
	}

	m_audio_fill_index = 0;
}

void firebeat_extend_spectrum_analyzer_device::sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs)
{
	// call the normal interface to actually mix
	device_mixer_interface::sound_stream_update(stream, inputs, outputs);

	// now consume the outputs
	for (int pos = 0; pos < outputs[0].samples(); pos++)
	{
		for (int ch = 0; ch < outputs.size(); ch++)
		{
			const float sample = outputs[ch].get(pos);
			m_audio_buf[m_audio_fill_index][ch][m_audio_count[m_audio_fill_index]] = sample;
		}

        update_fft();
	}

    double notches[] = { 95, 240, 600, 1500, 3400, 9200, 18000 };
    auto last_notch = std::end(notches) - std::begin(notches);

    auto srate = stream.sample_rate();
    auto order = WDL_fft_permute_tab(FFT_LENGTH / 2);
    for (int ch = 0; ch < TOTAL_CHANNELS; ch++) {
        double notch_max[TOTAL_BARS] = { -1, -1, -1, -1, -1, -1 };
    	int cur_notch = 0;

        for (int i = 0; i <= FFT_LENGTH / 2; i++) {
            WDL_FFT_COMPLEX* bin = (WDL_FFT_COMPLEX*)m_fft_buf[ch] + order[i];

            double re = 0, im = 0;
            if (i == 0)
            {
                // DC (0 Hz)
                re = bin->re;
                im = 0.0;
            }
            else if (i == FFT_LENGTH / 2)
            {
                // Nyquist frequency
                re = m_fft_buf[ch][1]; // i.e. DC bin->im
                im = 0.0;
            }
            else
            {
                re = bin->re;
                im = bin->im;
            }

            const double freq = (double)i / FFT_LENGTH * srate;
            const double mag = sqrt(re*re + im*im);

            if (freq >= notches[cur_notch+1]) {
                cur_notch++;
            }

            if (cur_notch + 1 > last_notch) {
                break;
            }

            if (notch_max[cur_notch] == -1 && freq >= notches[cur_notch] && freq < notches[cur_notch+1]) {
                notch_max[cur_notch] = mag;
            }
        }

        for (int i = 0; i < TOTAL_BARS; i++) {
            double val = log10(notch_max[i] * 4096) * 20;
			val = std::max<double>(0, val);
			m_bars[ch][i] = uint32_t(std::min<double>(val, 255.0f));
        }
    }
}

void firebeat_extend_spectrum_analyzer_device::apply_fft(uint32_t buf_index)
{
	float *audio_l = m_audio_buf[buf_index][0];
	float *audio_r = m_audio_buf[buf_index][1];
	float *buf_l = m_fft_buf[0];
	float *buf_r = m_fft_buf[1];

	for (int i = 0; i < FFT_LENGTH; i++)
	{
		*buf_l++ = *audio_l++;
		*buf_r++ = *audio_r++;
	}

	for (int ch = 0; ch < TOTAL_CHANNELS; ch++) {
		WDL_real_fft((WDL_FFT_REAL*)m_fft_buf[ch], FFT_LENGTH, 0);

		for (int i = 0; i < FFT_LENGTH; i++) {
			m_fft_buf[ch][i] /= (WDL_FFT_REAL)FFT_LENGTH;
		}
	}
}

void firebeat_extend_spectrum_analyzer_device::update_fft()
{
	m_audio_count[m_audio_fill_index]++;
	if (m_audio_count[m_audio_fill_index] >= FFT_LENGTH)
	{
		apply_fft(m_audio_fill_index);

		m_audio_fill_index = 1 - m_audio_fill_index;
		m_audio_count[m_audio_fill_index] = 0;
	}
}

int firebeat_extend_spectrum_analyzer_device::get_bar_value(int channel, int bar) {
    if (channel > TOTAL_CHANNELS || bar > TOTAL_BARS) {
        return 0;
    }

    return m_bars[channel][bar];
}

DEFINE_DEVICE_TYPE(KONAMI_FIREBEAT_EXTEND_SPECTRUM_ANALYZER, firebeat_extend_spectrum_analyzer_device, "firebeat_analyzer", "Firebeat Audio Visualizer (for beatmania III)")