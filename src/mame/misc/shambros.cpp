// license:BSD-3-Clause
// copyright-holders:windyfairy

/*
Kato's STV01 MainPCB Rev. C


Only known game: Shamisen Brothers Vol 1 (2003)
The game was also ported for Namco System 10

The dumper only had the CD-ROM.
A low quality picture of the PCB found on the internet shows:
- M68K-based processor
- program ROM
- multiple flash chips
- 3 FPGAs (Actel A54SX08A + others?)
- Mitsumi CD drive (FX5400W)
*/

#include "emu.h"
#include "shambros_a.h"
#include "shambros_v.h"

#include "bus/ata/ataintf.h"
#include "bus/ata/cr589.h"
#include "cpu/m68000/m68000.h"
#include "machine/intelfsh.h"
#include "machine/timer.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"


namespace {

class shambros_state : public driver_device
{
public:
	shambros_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_ata(*this, "ata")
		, m_flash(*this, "flash%u", 1)
		, m_video_flash(*this, "video_flash")
		, m_video(*this, "video")
		, m_sound(*this, "pcm_sound")
	{}

	void shambros(machine_config &config) ATTR_COLD;

protected:
	virtual void machine_start() override;
	virtual void machine_reset() override;

private:
	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	void cpu_map(address_map &map);

	void device_select_w(uint16_t data);
	void device_write_enable_w(uint16_t data);

	void device_w(offs_t offset, uint16_t data);
	uint16_t device_r(offs_t offset);

	TIMER_DEVICE_CALLBACK_MEMBER(music_timer);

	required_device<cpu_device> m_maincpu;
	required_device<ata_interface_device> m_ata;
	required_device_array<intelfsh16_device, 2> m_flash;
	required_device<intelfsh16_device> m_video_flash;
	required_device<shambros_video_device> m_video;
	required_device<shambros_sound_device> m_sound;

	uint16_t m_current_device;
	bool m_current_device_write_enable;
};

void shambros_state::machine_start()
{
	save_item(NAME(m_current_device));
	save_item(NAME(m_current_device_write_enable));
}

void shambros_state::machine_reset()
{
	m_current_device = 0;
	m_current_device_write_enable = false;
}

uint32_t shambros_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	return m_video->draw(screen, bitmap, cliprect);
}

TIMER_DEVICE_CALLBACK_MEMBER(shambros_state::music_timer)
{
	// IRQ 6 is responsible for incrementing the in-game song sync timer
	// If the IRQ is too fast or too slow then it effects a lot of things: the marker places, the note placements, the timing judgements
	m_maincpu->set_input_line(6, HOLD_LINE);
}

void shambros_state::device_select_w(uint16_t data)
{
	m_current_device = data;
}

void shambros_state::device_write_enable_w(uint16_t data)
{
	// Don't understand the difference between 0x500024 and 0x500026
	// This register seems to be written to when a write is involved, and set back to 0 when it's finished writing.
	// When only a read is involved, only 0x500024 needs to be set.
	// 0x500024 does not need to be set when a write is involved.
	m_current_device_write_enable = data != 0;
}

void shambros_state::device_w(offs_t offset, uint16_t data)
{
	if (m_current_device >= 0 && m_current_device <= 2) {
		m_sound->write(offset + (0x100000 * m_current_device), data);
	} else if (m_current_device == 3 && m_current_device_write_enable) {
		m_flash[0]->write(offset, data);
	} else if (m_current_device == 4 && m_current_device_write_enable) {
		m_flash[1]->write(offset, data);
	}
}

uint16_t shambros_state::device_r(offs_t offset)
{
	if (m_current_device >= 0 && m_current_device <= 2) {
		return m_sound->read(offset + (0x100000 * m_current_device));
	} else if (m_current_device == 3) {
		return m_flash[0]->read(offset);
	} else if (m_current_device == 4) {
		return m_flash[1]->read(offset);
	}

	return 0;
}

void shambros_state::cpu_map(address_map &map)
{
	map(0x000000, 0x3fffff).rom();
	map(0x200000, 0x223bff).ram(); // System memory?
	map(0x23e000, 0x3fffff).ram();

	map(0x400000, 0x400001).noprw(); // Read every time IRQ2 is called but doesn't seem to be used?

	map(0x500000, 0x500001).portr("IN1");
	map(0x500002, 0x500003).portr("IN2");
	map(0x500004, 0x500005).nopw(); // Lamps?
	map(0x500006, 0x500007).nopw(); // Also lamps?
	map(0x500020, 0x500021).rw(m_sound, FUNC(shambros_sound_device::voice_state_r), FUNC(shambros_sound_device::voice_state_w));
	map(0x500024, 0x500025).w(FUNC(shambros_state::device_select_w));
	map(0x500026, 0x500027).w(FUNC(shambros_state::device_write_enable_w));

	map(0x70000c, 0x70000d).nopw(); // 2 is written here before CD-ROM commands are sent?
	map(0x700010, 0x70001f).rw(m_ata, FUNC(ata_interface_device::cs0_r), FUNC(ata_interface_device::cs0_w));

	map(0x800000, 0x80ffff).m(m_video, FUNC(shambros_video_device::ram_map));
	map(0x810000, 0x81001f).m(m_video, FUNC(shambros_video_device::reg_map));

	map(0xa00000, 0xbfffff).rw(m_video, FUNC(shambros_video_device::data_r), FUNC(shambros_video_device::data_w));
	map(0xc00000, 0xdfffff).rw(FUNC(shambros_state::device_r), FUNC(shambros_state::device_w));
}


static INPUT_PORTS_START( shambros )
	PORT_START("IN1")
	PORT_BIT( 0x3f00, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(1) PORT_NAME("Neck Upper") // 1P UP
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(1) PORT_NAME("Neck Center") // 1P MID
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_BUTTON5 ) PORT_PLAYER(1) PORT_NAME("Neck Lower") // 1P LOW
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_BUTTON6 ) PORT_PLAYER(1) PORT_NAME("Bachi") // 1P SHOT
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_BUTTON7 ) PORT_PLAYER(2) PORT_NAME("Neck Upper") // 2P UP
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_BUTTON8 ) PORT_PLAYER(2) PORT_NAME("Neck Center") // 2P MID
	PORT_BIT( 0x0040, IP_ACTIVE_LOW, IPT_BUTTON9 ) PORT_PLAYER(2) PORT_NAME("Neck Lower") // 2P LOW
	PORT_BIT( 0x0080, IP_ACTIVE_LOW, IPT_BUTTON10 ) PORT_PLAYER(2) PORT_NAME("Bachi") // 2P SHOT
	PORT_BIT( 0x4000, IP_ACTIVE_HIGH, IPT_UNKNOWN ) // 1P DETECT
	PORT_BIT( 0x8000, IP_ACTIVE_HIGH, IPT_UNKNOWN ) // 2P DETECT

	PORT_START("IN2")
	PORT_BIT( 0xfe43, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) // select up
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) // select down
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_START ) // enter
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_COIN1 ) // coin
	PORT_BIT( 0x0080, IP_ACTIVE_LOW, IPT_SERVICE1 ) // service button
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_SERVICE ) // test menu button
INPUT_PORTS_END


void shambros_state::shambros(machine_config &config)
{
	M68000(config, m_maincpu, 16'000'000); // exact type not known, XTAL unreadable
	m_maincpu->set_addrmap(AS_PROGRAM, &shambros_state::cpu_map);

	// all wrong
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_RASTER));
	screen.set_refresh_hz(60);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(2500));
	screen.set_size(496, 384);
	screen.set_visarea(0, 336-1, 0, 240-1);
	screen.set_screen_update(FUNC(shambros_state::screen_update));
	screen.screen_vblank().set_inputline(m_maincpu, M68K_IRQ_2);
	screen.set_palette("palette");

	PALETTE(config, "palette", palette_device::BGR_555);

	ATA_INTERFACE(config, m_ata).options(ata_devices, "cdrom", nullptr, true);

	FUJITSU_29DL164BD_16BIT(config, m_flash[0]);
	FUJITSU_29DL164BD_16BIT(config, m_flash[1]);
	FUJITSU_29DL164BD_16BIT(config, m_video_flash);

	SHAMBROS_VIDEO(config, m_video, 0);

	SPEAKER(config, "lspeaker").front_left();
	SPEAKER(config, "rspeaker").front_right();

	SHAMBROS_SOUND(config, m_sound, XTAL(12'288'000)); // not right, 32000hz*384
	m_sound->add_route(0, "lspeaker", 0.5);
	m_sound->add_route(1, "rspeaker", 0.5);

	TIMER(config, "music_timer").configure_periodic(FUNC(shambros_state::music_timer), attotime::from_hz(250));
}


ROM_START( shambros )
	ROM_REGION(0x400000, "maincpu", 0)
	ROM_LOAD( "g112 v1.01.prg", 0x00000, 0x20000, NO_DUMP ) // actual size unknown. contains the bootloader + system font, among other things
	ROM_LOAD_OPTIONAL( "ascii16.pac", 0x020000, 0x1025c, CRC(f6fe3a43) SHA1(bd7ad57478f85d4f8a0614fc6c83f0a0e886b2ff) ) // custom hand crafted data
	ROM_LOAD( "pgx.bin", 0x000000, 0x19ab2, CRC(37c8da72) SHA1(89ab2786901ba422af9af972104fb79d679c1df6) ) // extracted from CD, this is the main program the bootloader executes
	ROM_COPY( "maincpu", 0x000000, 0x223c00, 0x19ab2 ) // copy it into the memory location that's expected for the program to be executed from

	DISK_REGION( "ata:0:cdrom" )
	DISK_IMAGE( "sb01-100", 0, SHA1(abd1d61871bcb4635acc691e35ec386823763ba2) )
ROM_END

} // anonymous namespace


GAME( 2003, shambros, 0, shambros, shambros, shambros_state, empty_init, ROT0, "Kato's", "Shamisen Brothers Vol 1", MACHINE_IS_SKELETON )
