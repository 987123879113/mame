#include "emu.h"
#include "shambros_sound.h"

DEFINE_DEVICE_TYPE(SHAMBROS_SOUND, shambros_sound_device, "shambros_sound", "Shamisen Brothers Sound")

shambros_sound_device::shambros_sound_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, SHAMBROS_SOUND, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_stream(nullptr)
	, m_flash(*this, ":flash%u", 1)
{
}

uint16_t shambros_sound_device::read(offs_t offset, uint16_t mem_mask)
{
	if (offset < 0x100 / 2)
		return m_regs[offset];

	return (m_ram[offset * 2] << 8) | m_ram[(offset * 2) + 1];
}

void shambros_sound_device::write(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (offset < 0x100 / 2)
	{
		const int ch = offset >> 4;
		const int reg = offset & 0xf;

		m_regs[offset] = data;

		switch (reg)
		{
			case 1:
				m_voices[ch].addr_cur &= 0xffff0000000;
				m_voices[ch].addr_cur |= uint64_t(data) << 12;
				printf("voice[%d].addr_cur = %08llx, is_looped = %d\n", ch, m_voices[ch].addr_cur >> 12, m_voices[ch].is_looped);
				break;
			case 2:
				m_voices[ch].addr_cur &= 0x0000ffff000;
				m_voices[ch].addr_cur |= uint64_t(data & 0x7fff) << 28;
				m_voices[ch].is_looped = BIT(data, 15) != 0;
				printf("voice[%d].addr_cur = %08llx, is_looped = %d\n", ch, m_voices[ch].addr_cur >> 12, m_voices[ch].is_looped);
				break;

			case 3:
				/*
				0x499 (1177) -> 11025 hz
				0xad6 (2774) -> 22050 hz
				0xd59 (3417) -> 32000 hz
				*/
				m_voices[ch].step = data;
				printf("voice[%d].step = %08x\n", ch, m_voices[ch].step);
				break;

			case 4:
				m_voices[ch].addr_loop &= 0xffff0000000;
				m_voices[ch].addr_loop |= uint64_t(data) << 12;
				printf("voice[%d].addr_loop = %08llx\n", ch, m_voices[ch].addr_loop >> 12);
				break;
			case 5:
				m_voices[ch].addr_loop &= 0x0000ffff000;
				m_voices[ch].addr_loop |= uint64_t(data) << 28;
				printf("voice[%d].addr_loop = %08llx\n", ch, m_voices[ch].addr_loop >> 12);
				break;

			case 6:
				m_voices[ch].addr_end &= 0xffff0000000;
				m_voices[ch].addr_end |= uint64_t(data) << 12;
				printf("voice[%d].addr_end = %08llx\n", ch, m_voices[ch].addr_end >> 12);
				break;
			case 7:
				m_voices[ch].addr_end &= 0x0000ffff000;
				m_voices[ch].addr_end |= uint64_t(data) << 28;
				printf("voice[%d].addr_end = %08llx\n", ch, m_voices[ch].addr_end >> 12);
				break;

			case 0x0b:
				m_voices[ch].vol_l = data;
				printf("voice[%d].vol_l = %08x\n", ch, m_voices[ch].vol_l);
				break;
			case 0x0c:
				m_voices[ch].vol_r = data;
				printf("voice[%d].vol_r = %08x\n", ch, m_voices[ch].vol_r);
				break;

			default:
				printf("Unknown register usage: channel %d, register %x, data %04x\n", ch, reg, data);
				break;
		}
	}
	else
	{
		m_ram[offset * 2] = BIT(data, 8, 8);
		m_ram[(offset * 2) + 1] = BIT(data, 0, 8);
	}
}

uint16_t shambros_sound_device::channel_state_r()
{
	uint16_t r = 0;

	for (int ch = 0; ch < 8; ch++)
		r |= (m_voices[ch].enabled ? 1 : 0) << ch;

	return r;
}

void shambros_sound_device::channel_state_w(uint16_t data)
{
	for (int ch = 0; ch < 8; ch++)
		m_voices[ch].enabled = BIT(data, ch);

	m_stream->update();
}

void shambros_sound_device::device_start()
{
	m_stream = stream_alloc(0, 2, clock() / 384);

	m_ram = make_unique_clear<uint8_t[]>(0x600000);
	std::fill(std::begin(m_regs), std::end(m_regs), 0);

	for (int i = 0; i < 8; i++)
	{
		m_voices[i].addr_loop = 0;
		m_voices[i].addr_cur = 0;
		m_voices[i].addr_end = 0;
		m_voices[i].vol_l = 0;
		m_voices[i].vol_r = 0;
		m_voices[i].step = 0;
		m_voices[i].is_looped = false;
		m_voices[i].enabled = false;
	}
}

int8_t shambros_sound_device::get_sample(uint32_t offset)
{
	if (offset >= 0x600000) {
		uint16_t r = 0;

		if (offset >= 0x600000 && offset < 0x800000)
			r = m_flash[0]->read_raw((offset - 0x600000) / 2);
		else if (offset >= 0x800000 && offset < 0xa00000)
			r = m_flash[1]->read_raw((offset - 0x800000) / 2);

		if ((offset & 1) == 0)
			r >>= 8;

		return r & 0xff;
	}

	return m_ram[offset];
}

void shambros_sound_device::sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs)
{
	outputs[0].fill(0);
	outputs[1].fill(0);

	for (int ch = 0; ch < 8; ch++)
	{
		shambros_channel *channel = &m_voices[ch];

		if (!channel->enabled || channel->addr_cur >= channel->addr_end)
			continue;

		for (int i = 0; i < outputs[0].samples() && channel->enabled; i++)
		{
			if (channel->is_looped && channel->addr_cur >= channel->addr_end)
				channel->addr_cur = channel->addr_loop;

			const uint32_t offset = channel->addr_cur >> 12;
			const int sample = get_sample(offset) * 256;
			channel->addr_cur += channel->step;

			outputs[0].add_int(i, sample * (channel->vol_l / 65535.0), 32768);
			outputs[1].add_int(i, sample * (channel->vol_r / 65535.0), 32768);

			if (!channel->is_looped && channel->addr_cur >= channel->addr_end)
				channel->enabled = false;
		}
	}
}
