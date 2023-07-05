// license:BSD-3-Clause
// copyright-holders:windyfairy
#ifndef MAME_MISC_SHAMBROS_A_H
#define MAME_MISC_SHAMBROS_A_H

#pragma once

#include "machine/intelfsh.h"

class shambros_sound_device : public device_t,
					   public device_sound_interface
{
public:
	static constexpr feature_type imperfect_features() { return feature::SOUND; } // unemulated and/or unverified effects and envelopes

	shambros_sound_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	uint16_t read(offs_t offset, uint16_t mem_mask = ~0);
	void write(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	uint16_t voice_state_r();
	void voice_state_w(uint16_t data);

protected:
	virtual void device_start() override;
	virtual void device_reset() override;

	virtual void sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs) override;

private:
	int8_t get_sample(uint32_t offset);

	std::unique_ptr<uint8_t[]> m_ram;
	uint16_t m_regs[128];

	struct shambros_voice {
		uint64_t addr_loop;
		uint64_t addr_cur;
		uint64_t addr_end;
		uint16_t vol_l;
		uint16_t vol_r;
		uint16_t step;
		bool is_looped;
		bool enabled;
	};

	shambros_voice m_voices[8];

	sound_stream *m_stream;

	required_device_array<intelfsh16_device, 2> m_flash;
};

DECLARE_DEVICE_TYPE(SHAMBROS_SOUND, shambros_sound_device)

#endif // MAME_MISC_SHAMBROS_A_H
