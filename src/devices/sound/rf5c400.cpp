// license:BSD-3-Clause
// copyright-holders:Ville Linde
/*
    Ricoh RF5C400 emulator

    Written by Ville Linde
    Improvements by the hoot development team

    history -
    2007-02-08 hoot development team
        looping
        stereo panning
        8-bit sample support

    2007-02-16 hoot development team
        envelope
        fixed volume table
*/

#include "emu.h"
#include "rf5c400.h"

namespace {

int volume_table[256];
double pan_table[0x64];

void init_static_tables()
{
	// init volume/pan tables
	double max = 255.0;
	for (int i = 0; i < 256; i++) {
		volume_table[i] = uint16_t(max);
		max /= pow(10.0, double((4.5 / (256.0 / 16.0)) / 20));
	}
	for (int i = 0; i < 0x48; i++) {
		pan_table[i] = sqrt(double(0x47 - i)) / sqrt(double(0x47));
	}
	for (int i = 0x48; i < 0x64; i++) {
		pan_table[i] = 0.0;
	}
}


/* PCM type */
enum
{
	TYPE_MASK       = 0x00C0,
	TYPE_16         = 0x0000,
	TYPE_8LOW       = 0x0040,
	TYPE_8HIGH      = 0x0080
};

/* envelope phase */
enum
{
	PHASE_NONE      = 0,
	PHASE_ATTACK,
	PHASE_DECAY,
	PHASE_RELEASE
};

} // anonymous namespace


// device type definition
DEFINE_DEVICE_TYPE(RF5C400, rf5c400_device, "rf5c400", "Ricoh RF5C400")


rf5c400_device::envelope_tables::envelope_tables()
{
	std::fill(std::begin(m_ar), std::end(m_ar), 0.0);
	std::fill(std::begin(m_dr), std::end(m_dr), 0.0);
	std::fill(std::begin(m_rr), std::end(m_rr), 0.0);
}

void rf5c400_device::envelope_tables::init(uint32_t clock)
{
	/* envelope parameter (experimental) */
	static constexpr double ENV_AR_SPEED    = 0.1;
	static constexpr int    ENV_MIN_AR      = 0x02;
	static constexpr int    ENV_MAX_AR      = 0x80;
	static constexpr double ENV_DR_SPEED    = 2.0;
	static constexpr int    ENV_MIN_DR      = 0x20;
	static constexpr int    ENV_MAX_DR      = 0x73;
	static constexpr double ENV_RR_SPEED    = 0.7;
	static constexpr int    ENV_MIN_RR      = 0x20;
	static constexpr int    ENV_MAX_RR      = 0x54;

	double r;

	// attack
	r = 1.0 / (ENV_AR_SPEED * (clock / 384));
	for (int i = 0; i < ENV_MIN_AR; i++)
		m_ar[i] = 1.0;
	for (int i = ENV_MIN_AR; i < ENV_MAX_AR; i++)
		m_ar[i] = r * (ENV_MAX_AR - i) / (ENV_MAX_AR - ENV_MIN_AR);
	for (int i = ENV_MAX_AR; i < 0x9f; i++)
		m_ar[i] = 0.0;

	// decay
	r = -5.0 / (ENV_DR_SPEED * (clock / 384));
	for (int i = 0; i < ENV_MIN_DR; i++)
		m_dr[i] = r;
	for (int i = ENV_MIN_DR; i < ENV_MAX_DR; i++)
		m_dr[i] = r * (ENV_MAX_DR - i) / (ENV_MAX_DR - ENV_MIN_DR);
	for (int i = ENV_MAX_DR; i < 0x9f; i++)
		m_dr[i] = 0.0;

	// release
	r = -5.0 / (ENV_RR_SPEED * (clock / 384));
	for (int i = 0; i < ENV_MIN_RR; i++)
		m_rr[i] = r;
	for (int i = ENV_MIN_RR; i < ENV_MAX_RR; i++)
		m_rr[i] = r * (ENV_MAX_RR - i) / (ENV_MAX_RR - ENV_MIN_RR);
	for (int i = ENV_MAX_RR; i < 0x9f; i++)
		m_rr[i] = 0.0;
}


//**************************************************************************
//  LIVE DEVICE
//**************************************************************************

//-------------------------------------------------
//  rf5c400_device - constructor
//-------------------------------------------------

rf5c400_device::rf5c400_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, RF5C400, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, device_rom_interface(mconfig, *this)
	, m_stream(nullptr)
	, m_env_tables()
{
}



//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void rf5c400_device::device_start()
{
	init_static_tables();
	m_env_tables.init(clock());

	// init channel info
	for (rf5c400_channel &chan : m_channels)
	{
		chan.startH = 0;
		chan.startL = 0;
		chan.freq = 0;
		chan.endL = 0;
		chan.endHloopH = 0;
		chan.loopL = 0;
		chan.pan = 0;
		chan.effect = 0;
		chan.volume = 0;
		chan.attack = 0;
		chan.decay = 0;
		chan.release = 0;
		chan.pos = 0;
		chan.step = 0;
		chan.keyon = 0;
		chan.env_phase = PHASE_NONE;
		chan.env_level = 0.0;
		chan.env_step = 0.0;
		chan.env_scale = 1.0;
		chan.start_pos = 0;
		chan.offset = 0;
	}

	save_item(NAME(m_rf5c400_status));
	save_item(NAME(m_ext_mem_address));
	save_item(NAME(m_ext_mem_data));
	save_item(NAME(m_requested_channel));
	save_item(NAME(m_requested_cmd));

	save_item(STRUCT_MEMBER(m_channels, startH));
	save_item(STRUCT_MEMBER(m_channels, startL));
	save_item(STRUCT_MEMBER(m_channels, freq));
	save_item(STRUCT_MEMBER(m_channels, endL));
	save_item(STRUCT_MEMBER(m_channels, endHloopH));
	save_item(STRUCT_MEMBER(m_channels, loopL));
	save_item(STRUCT_MEMBER(m_channels, pan));
	save_item(STRUCT_MEMBER(m_channels, effect));
	save_item(STRUCT_MEMBER(m_channels, volume));
	save_item(STRUCT_MEMBER(m_channels, attack));
	save_item(STRUCT_MEMBER(m_channels, decay));
	save_item(STRUCT_MEMBER(m_channels, release));
	save_item(STRUCT_MEMBER(m_channels, cutoff));
	save_item(STRUCT_MEMBER(m_channels, pos));
	save_item(STRUCT_MEMBER(m_channels, step));
	save_item(STRUCT_MEMBER(m_channels, keyon));
	save_item(STRUCT_MEMBER(m_channels, env_phase));
	save_item(STRUCT_MEMBER(m_channels, env_level));
	save_item(STRUCT_MEMBER(m_channels, env_step));
	save_item(STRUCT_MEMBER(m_channels, env_scale));
	save_item(STRUCT_MEMBER(m_channels, start_pos));
	save_item(STRUCT_MEMBER(m_channels, offset));

	m_stream = stream_alloc(0, 2, clock() / 384, STREAM_SYNCHRONOUS);
}

//-------------------------------------------------
//  device_clock_changed - called if the clock
//  changes
//-------------------------------------------------

void rf5c400_device::device_clock_changed()
{
	m_env_tables.init(clock());
	m_stream->set_sample_rate(clock() / 384);
}

//-------------------------------------------------
//  sound_stream_update - handle a stream update
//-------------------------------------------------

void rf5c400_device::sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs)
{
	int i, ch;
	uint64_t end, loop;
	uint64_t pos;
	uint8_t vol, lvol, rvol, type;
	uint8_t env_phase;
	double env_level, env_step, env_rstep;

	outputs[0].fill(0);
	outputs[1].fill(0);

	for (ch=0; ch < 32; ch++)
	{
		rf5c400_channel *channel = &m_channels[ch];
		auto &buf0 = outputs[0];
		auto &buf1 = outputs[1];

		auto offset = channel->offset;
		end = ((channel->endHloopH & 0xFF) << 16) | channel->endL;
		loop = ((channel->endHloopH & 0xFF00) << 8) | channel->loopL;
		pos = channel->pos;
		vol = channel->volume & 0xFF;
		lvol = channel->pan & 0xFF;
		rvol = channel->pan >> 8;
		type = (channel->volume >> 8) & TYPE_MASK;

		env_phase = channel->env_phase;
		env_level = channel->env_level;
		env_step = channel->env_step;
		env_rstep = env_step * channel->env_scale;

		for (i=0; i < buf0.samples(); i++)
		{
			int16_t tmp;
			int32_t sample;

			if (env_phase == PHASE_NONE) break;

			tmp = read_word((pos>>16)<<1);
			switch ( type )
			{
				case TYPE_16:
					sample = tmp;
					break;
				case TYPE_8LOW:
					sample = (int16_t)(tmp << 8);
					break;
				case TYPE_8HIGH:
					sample = (int16_t)(tmp & 0xFF00);
					break;
				default:
					sample = 0;
					break;
			}

			if ( sample & 0x8000 )
			{
				sample ^= 0x7FFF;
			}

			env_level += env_rstep;
			switch (env_phase)
			{
			case PHASE_ATTACK:
				if (env_level >= 1.0)
				{
					env_phase = PHASE_DECAY;
					env_level = 1.0;
					if ((channel->decay & 0x0080) || (channel->decay == 0x100))
					{
						env_step = 0.0;
					}
					else
					{
						env_step = m_env_tables.dr(*channel);
					}
					env_rstep = env_step * channel->env_scale;
				}
				break;
			case PHASE_DECAY:
				if (env_level <= 0.0)
				{
					env_phase = PHASE_NONE;
					env_level = 0.0;
					env_step = 0.0;
					env_rstep = 0.0;
				}
				break;
			case PHASE_RELEASE:
				if (env_level <= 0.0)
				{
					env_phase = PHASE_NONE;
					env_level = 0.0;
					env_step = 0.0;
					env_rstep = 0.0;
				}
				break;
			}

			sample *= volume_table[vol];
			sample = (sample >> 9) * env_level;
			buf0.add_int(i, sample * pan_table[lvol], 32768);
			buf1.add_int(i, sample * pan_table[rvol], 32768);

			pos += channel->step;
			offset += channel->step;
			if ((pos>>16) > end)
			{
				offset = 0;

				if (loop > end) {
					pos = channel->start_pos;
				}
				else {
					pos -= loop << 16;
					pos &= 0xFFFFFF0000ULL;
				}
			}
		}

		channel->offset = offset;
		channel->pos = pos;
		channel->env_phase = env_phase;
		channel->env_level = env_level;
		channel->env_step = env_step;
	}
}

void rf5c400_device::rom_bank_updated()
{
	m_stream->update();
}

/*****************************************************************************/

uint16_t rf5c400_device::rf5c400_r(offs_t offset, uint16_t mem_mask)
{
	if (offset < 0x400)
	{
		//osd_printf_debug("%s:rf5c400_r: %08X, %08X\n", machine().describe_context(), offset, mem_mask);

		switch(offset)
		{
			case 0x00:
			{
				return m_rf5c400_status;
			}

			case 0x04:      // unknown read
			{
				return 0;
			}

			case 0x09:      // position read?
			{
				if (m_requested_cmd != 6) {
					//printf("Unknown m_requested_cmd: %04x on ch %d\n", m_requested_cmd, m_requested_channel);
				}

				rf5c400_channel* channel = &m_channels[m_requested_channel];

				if (channel->env_phase == PHASE_NONE) {
					return 0;
				}

				// pop'n music's SPU program expects to read this register 6 times with the same value every
				// read, and then the value should match a different value in memory (@ 0x1008ca).
				// The value seems to correspond to how much data is read during the DMAs.
				// The first DMA for pop'n music is 0x200000 bytes and then subsequent DMAs are 0x100000 bytes.
				// The first value matched at 0x1008ca in the SPU program is 2, and then after that 1s.
				// When the value matches, the SPU sends off a new DMA request to overwrite the sample data in
				// memory. This DMA request completely overwrites the currently playing sample data.
				//
				// TODO: There's no reason for the +4 to actually be here in a proper implementation.
				auto ret = (uint16_t)(((channel->offset >> 16) >> 7) + 4) & 0xffff;

				//printf("SPU read pos: %08x\n", ret);

				return ret;
			}

			case 0x13:      // memory read
			{
				return read_word(m_ext_mem_address<<1);
			}

			default:
			{
				//osd_printf_debug("%s:rf5c400_r: %08X, %08X\n", machine().describe_context(), offset, mem_mask);
				return 0;
			}
		}
	}
	else
	{
		//int ch = (offset >> 5) & 0x1f;
		int reg = (offset & 0x1f);

		switch (reg)
		{
		case 0x0F:      // unknown read
			return 0;

		default:
			return 0;
		}
	}
}

void rf5c400_device::rf5c400_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (offset < 0x400)
	{
		if (offset != 8 && offset != 0x11 && offset != 0x12 && offset != 0x13 && offset != 0x14) {
			//printf("%lf: offset %04x, data %04x\n", machine().time().as_double(), offset, data);
		}

		switch(offset)
		{
			case 0x00:
			{
				m_rf5c400_status = data;
				break;
			}

			case 0x01:      // channel control
			{
				int ch = data & 0x1f;
				switch ( data & 0x60 )
				{
					case 0x60:
						m_channels[ch].offset = 0;
						m_channels[ch].pos = ((m_channels[ch].startH & 0xFF00) << 8) | m_channels[ch].startL;
						m_channels[ch].pos <<= 16;
						m_channels[ch].start_pos = m_channels[ch].pos;

						m_channels[ch].env_phase = PHASE_ATTACK;
						m_channels[ch].env_level = 0.0;
						m_channels[ch].env_step  = m_env_tables.ar(m_channels[ch]);

						break;
					case 0x40:
						if (m_channels[ch].env_phase != PHASE_NONE)
						{
							m_channels[ch].env_phase = PHASE_RELEASE;
							if (m_channels[ch].release & 0x0080)
							{
								m_channels[ch].env_step = 0.0;
							}
							else
							{
								m_channels[ch].env_step = m_env_tables.rr(m_channels[ch]);
							}
						}
						break;
					default:
						m_channels[ch].env_phase = PHASE_NONE;
						m_channels[ch].env_level = 0.0;
						m_channels[ch].env_step  = 0.0;
						break;
				}


				{
					//auto start = ((m_channels[ch].startH & 0xFF00) << 8) | m_channels[ch].startL;
					//auto end = ((m_channels[ch].endHloopH & 0xFF) << 16) | m_channels[ch].endL;
					//auto loop = ((m_channels[ch].endHloopH & 0xFF00) << 8) | m_channels[ch].loopL;
					//printf("ch %d, start: %08x, stop: %08x, loop: %08x\n", ch, start, end, loop);
				}

				break;
			}

			case 0x08:      // channel request??
			{
				// This is called before every 0x09 read in pop'n music's SPU
				int ch = data & 0x1f;
				m_requested_channel = ch;
				m_requested_cmd = data >> 5;
				break;
			}


			case 0x09:      // relative to env attack (0x0c00/ 0x1c00)

			case 0x11:      // memory r/w address, bits 15 - 0
			{
				m_ext_mem_address &= ~0xffff;
				m_ext_mem_address |= data;
				break;
			}
			case 0x12:      // memory r/w address, bits 23 - 16
			{
				m_ext_mem_address &= 0xffff;
				m_ext_mem_address |= (uint32_t)(data) << 16;
				break;
			}
			case 0x13:      // memory write data
			{
				m_ext_mem_data = data;
				break;
			}

			case 0x14:      // memory write
			{
				if ((data & 0x3) == 3)
				{
					this->space().write_word(m_ext_mem_address << 1, m_ext_mem_data);
				}
				break;
			}

			case 0x21:      // reverb(character).w
			case 0x32:      // reverb(pre-lpf).w
			case 0x2B:      // reverb(level).w
			case 0x20:      // ???.b : reverb(time).b

			case 0x2C:      // chorus(level).w
			case 0x30:      // chorus(rate).w
			case 0x22:      // chorus(macro).w
			case 0x23:      // chorus(depth).w
			case 0x24:      // chorus(macro).w
			case 0x2F:      // chorus(depth).w
			case 0x27:      // chorus(send level to reverb).w

			default:
			{
				//osd_printf_debug("%s:rf5c400_w: %08X, %08X, %08X\n", machine().describe_context(), data, offset, mem_mask);
				break;
			}
		}
		//osd_printf_debug("%s:rf5c400_w: %08X, %08X, %08X\n", machine().describe_context(), data, offset, mem_mask);
	}
	else
	{
		// channel registers
		int ch = (offset >> 5) & 0x1f;
		int reg = (offset & 0x1f);

		rf5c400_channel *channel = &m_channels[ch];

		switch (reg)
		{
			case 0x00:      // sample start address, bits 23 - 16
			{
				channel->startH = data;
				break;
			}
			case 0x01:      // sample start address, bits 15 - 0
			{
				channel->startL = data;
				break;
			}
			case 0x02:      // sample playing frequency
			{
				channel->step = ((data & 0x1fff) << (data >> 13)) * 4;
				channel->freq = data;
				break;
			}
			case 0x03:      // sample end address, bits 15 - 0
			{
				channel->endL = data;
				break;
			}
			case 0x04:      // sample end address, bits 23 - 16 , sample loop 23 - 16
			{
				channel->endHloopH = data;
				break;
			}
			case 0x05:      // sample loop offset, bits 15 - 0
			{
				channel->loopL = data;
				break;
			}
			case 0x06:      // channel volume
			{
				channel->pan = data;
				break;
			}
			case 0x07:      // effect depth
			{
				// 0xCCRR: CC = chorus send depth, RR = reverb send depth
				channel->effect = data;
				break;
			}
			case 0x08:      // volume, flag
			{
				channel->volume = data;
				break;
			}
			case 0x09:      // env attack
			{
				// 0x0100: max speed                  (in case of attack <= 0x40)
				// 0xXX40: XX = attack-0x3f (encoded) (in case of attack > 0x40)
				//
				channel->attack = data;
				break;
			}
			case 0x0A:      // relative to env attack ?
			{
				// always 0x0100/0x140
				break;
			}
			case 0x0B:      // relative to env decay ?
			{
				// always 0x0100/0x140/0x180
				break;
			}
			case 0x0C:      // env decay
			{
				// 0xXX70: XX = decay (encoded) (in case of decay > 0x71)
				// 0xXX80: XX = decay (encoded) (in case of decay <= 0x71)
				channel->decay = data;
				break;
			}
			case 0x0D:      // relative to env release ?
			{
				// always 0x0100/0x140
				break;
			}
			case 0x0E:      // env release
			{
				// 0xXX70: XX = release-0x1f (encoded) (0x01 if release <= 0x20)
				channel->release = data;
				break;
			}
			case 0x0F:      // unknown write
			{
				// always 0x0000
				break;
			}
			case 0x10:      // resonance, cutoff freq.
			{
				// bit 15-12: resonance
				// bit 11-0 : cutoff frequency
				channel->cutoff = data;
				break;
			}
		}
	}
}
