// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Firebeat Extend Board
 * Used by Keyboardmania and beatmania III
 *
 * There are two sides to the extend board, CN1 and CN2.
 * These both connect to the backplane but seem to have different functionality.
 * Keyboardmania does not have CN2 or the spectrum analyzer circuit populated.
 *
 *
 * FDC and related IO connectors (CN1)
 * - beatmania III has a 44 pin FDD connector (CN9), 1 MIDI OUT (CN3) populated.
 * - Keyboardmania has 1 MIDI OUT (CN3), 2 MIDI INs (CN4 and CN5) populated.
 *
 *
 * Spectrum Analyzer (CN2)
 * ref: https://forum.cockos.com/showthread.php?t=231070
 *
 *
 * All(?) variations of the extend board have the following unused connectors:
 * - An unpopulated CN6 labeled JC26-FSRH16 (Compact Flash adapter)
 * - A populated CN7 XM3B-0922 (D-Sub connector)
 * - A populated CN8 Omron XG4 2-tier MIL connector (exact connector unknown)
 */

#include "emu.h"

#include "machine/firebeat_extend.h"

#include "wdlfft/fft.h"

#include <cmath>

firebeat_extend_device::firebeat_extend_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, KONAMI_FIREBEAT_EXTEND, tag, owner, clock),
    m_fdc(*this, "fdc"),
    m_duart(*this, "duart_midi"),
    m_spectrum_analyzer(*this, "spectrum_analyzer")
{
}

void firebeat_extend_device::device_start()
{
}

void firebeat_extend_device::device_add_mconfig(machine_config &config)
{
	FDC37C665GT(config, m_fdc, 24_MHz_XTAL, upd765_family_device::mode_t::PS2);
	PC16552D(config, m_duart, 0);
    KONAMI_FIREBEAT_EXTEND_SPECTRUM_ANALYZER(config, m_spectrum_analyzer, 0);
}

void firebeat_extend_device::extend_map(address_map &map)
{
	map(0x00000000, 0x00000fff).rw(FUNC(firebeat_extend_device::midi_uart_r), FUNC(firebeat_extend_device::midi_uart_w)).umask32(0xff000000);
    map(0x00001000, 0x00001fff).rw(m_fdc, FUNC(fdc37c665gt_device::read), FUNC(fdc37c665gt_device::write)).umask32(0xff000000);
}

void firebeat_extend_device::extend_map_bm3(address_map &map)
{
    extend_map(map);
	map(0x00008000, 0x0000807f).r(m_spectrum_analyzer, FUNC(firebeat_extend_spectrum_analyzer_device::read));
}

uint8_t firebeat_extend_device::midi_uart_r(offs_t offset)
{
	return m_duart->read(offset >> 6);
}

void firebeat_extend_device::midi_uart_w(offs_t offset, uint8_t data)
{
	m_duart->write(offset >> 6, data);
}

DEFINE_DEVICE_TYPE(KONAMI_FIREBEAT_EXTEND, firebeat_extend_device, "firebeat_extend", "Firebeat Extend Board")

/*****************************************************************************/

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

uint8_t firebeat_extend_spectrum_analyzer_device::read(offs_t offset)
{
	// Visible in the sound test menu and used for the spectral analyzer game skin
	//
	// Notes about where this could be coming from...
	// - It's not the ST-224: Only sends audio in and out, with a MIDI in
	// - It's not the RF5C400: There are no unimplemented registers or anything of that sort that could give this info
	// - The memory address mapping is the same as Keyboardmania's wheel, which plugs into a connector on extend board
	//   but there's nothing actually plugged into that spot on a beatmania III configuration, so it's not external
	// - Any place where the audio is directed somewhere (amps, etc) does not have a way to get back to the PCBs
	//   from what I can tell based on looking at the schematics in the beatmania III manual
	// - I think it's probably calculated somewhere within one of the main boards (main/extend/SPU) but couldn't find any
	//   potentially interesting chips at a glance of PCB pics
	// - The manual does not seem to make mention of this feature *at all* much less troubleshooting it, so no leads there

	// 6 notch spectrum analyzer
	// Notch 1 (90-240)
	// Notch 2 (240-600)
	// Notch 3 (600-1.5K)
	// Notch 4 (1.5K-3.4K)
	// Notch 5 (3.4K-9.2K)
	// Notch 6 (9.2K-18K)
	//
	// Return values notes:
	// - Anything lower than <= 8 will not display anything in-game
	// - The way this register is read is weird. It reads the upper and lower half of the register separately as bytes,
	// but it the upper byte doesn't seem like it's actually used
	// - In-game the skin shows up to +9 dB but it actually caps out at +3 dB on the skin

	int ch = offset >= 0x40; // 0 = Left, 1 = Right
	int notch = 6 - (((offset >> 2) & 0x0f) >> 1);
	int is_upper = !!(offset & 4);

	auto r = (ch < TOTAL_CHANNELS && notch < TOTAL_BARS) ? m_bars[ch][notch] : 0;

	if (is_upper) {
		return (r >> 8) & 0xff;
	}

	return r & 0xff;
}

DEFINE_DEVICE_TYPE(KONAMI_FIREBEAT_EXTEND_SPECTRUM_ANALYZER, firebeat_extend_spectrum_analyzer_device, "firebeat_extend_spectrum_analyzer", "Firebeat Extend Specetrum Analyzer")
