// license:BSD-3-Clause
// copyright-holders:windyfairy


#include "emu.h"
#include "cpu/h8/h83006.h"
#include "machine/intelfsh.h"
#include "machine/msm6242.h"
#include "video/hd44780.h"
#include "emupal.h"
#include "screen.h"


namespace {

class gu956_state : public driver_device
{
public:
	gu956_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_rtc(*this, "rtc")
		, m_lcdc(*this, "lcdc")
		, m_flash(*this, "flash")
	{
	}

	void gu956(machine_config &config);

protected:
	HD44780_PIXEL_UPDATE(pixel_update);

	void gu956_map(address_map &map);

	virtual void machine_start() override;
	virtual void machine_reset() override;

	void network_w(offs_t offset, uint8_t data);
	uint8_t network_r(offs_t offset);

    required_device<cpu_device> m_maincpu;
	required_device<msm6242_device> m_rtc;
	required_device<hd44780_device> m_lcdc;
	required_device<intelfsh8_device> m_flash;

	template <int Port> void port_write(offs_t offset, uint8_t data);
	template <int Port> uint8_t port_read();
};

HD44780_PIXEL_UPDATE(gu956_state::pixel_update)
{
	if (x < 5 && y < 8 && line < 2 && pos < 16)
		bitmap.pix(line * 8 + y, pos * 6 + x) = state;
}

void gu956_state::network_w(offs_t offset, uint8_t data)
{
	// offset 0 = register addr
	// offset 1 = value to write to register
	printf("%s network_w %02x %02x\n", machine().describe_context().c_str(), offset, data);
}

uint8_t gu956_state::network_r(offs_t offset)
{
	return 0;
}

void gu956_state::gu956_map(address_map &map)
{
	map(0x000000, 0x07ffff).rw(m_flash, FUNC(intelfsh8_device::read), FUNC(intelfsh8_device::write));
    map(0x200000, 0x20efff).ram(); // SRAM
	map(0x400000, 0x40000f).rw(m_rtc, FUNC(msm6242_device::read), FUNC(msm6242_device::write)); // RTC
	map(0x600000, 0x600000).rw(m_lcdc, FUNC(hd44780_device::db_r), FUNC(hd44780_device::db_w));
	map(0x800000, 0x800001).rw(FUNC(gu956_state::network_r), FUNC(gu956_state::network_w)); // LSI S7600A
	// A00000 ?
}

void gu956_state::machine_start()
{
}

void gu956_state::machine_reset()
{
}

template <int Port>
void gu956_state::port_write(offs_t offset, uint8_t data)
{
	logerror("%s: port%d_write %02x\n", machine().describe_context(), Port, data);

	if (Port == 10) {
		m_lcdc->rs_w(BIT(data, 0));
		m_lcdc->rw_w(BIT(data, 1));
		m_lcdc->e_w(BIT(data, 2));
	}
}

template <int Port>
uint8_t gu956_state::port_read()
{
	if (Port == 4)
		return 0xff; // Coin-in status

	if (Port == 9)
		return 0xff; // Battery status

	if (Port == 11)
		return 0xff; // BUSYX from S-7600A?

	logerror("%s: port%d_read\n", machine().describe_context(), Port);

	return 0;
}

void gu956_state::gu956(machine_config &config)
{
	auto &maincpu(H83007(config, m_maincpu, 20_MHz_XTAL));
	maincpu.set_addrmap(AS_PROGRAM, &gu956_state::gu956_map);
	maincpu.read_port4().set(FUNC(gu956_state::port_read<4>));
	maincpu.read_port6().set(FUNC(gu956_state::port_read<6>));
	maincpu.read_port7().set_ioport("DIPSW");
	maincpu.read_port8().set(FUNC(gu956_state::port_read<8>));
	maincpu.read_port9().set(FUNC(gu956_state::port_read<9>));
	maincpu.read_porta().set(FUNC(gu956_state::port_read<10>));
	maincpu.read_portb().set(FUNC(gu956_state::port_read<11>));
	maincpu.write_port4().set(FUNC(gu956_state::port_write<4>));
	maincpu.write_port6().set(FUNC(gu956_state::port_write<6>));
	maincpu.write_port8().set(FUNC(gu956_state::port_write<8>));
	maincpu.write_port9().set(FUNC(gu956_state::port_write<9>));
	maincpu.write_porta().set(FUNC(gu956_state::port_write<10>));
	maincpu.write_portb().set(FUNC(gu956_state::port_write<11>));

	AMD_29F040(config, m_flash);

	MSM6242(config, m_rtc, XTAL(32'768));

	HD44780(config, m_lcdc, 0);
	m_lcdc->set_lcd_size(2, 16);
	m_lcdc->set_pixel_update_cb(FUNC(gu956_state::pixel_update));

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(2500)); /* not accurate */
	screen.set_screen_update("lcdc", FUNC(hd44780_device::screen_update));
	screen.set_size(6*16, 8*2);
	screen.set_visarea_full();
	screen.set_palette("palette");

	PALETTE(config, "palette", palette_device::MONOCHROME_INVERTED);
}


static INPUT_PORTS_START( gu956 )
	PORT_START( "DIPSW" )
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_NAME("Set")
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_NAME("Menu")
	PORT_DIPNAME( 0x10, 0x10, "Test mode" ) PORT_DIPLOCATION( "DIP SW:1" )
	PORT_DIPSETTING(          0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, "Setup mode" ) PORT_DIPLOCATION( "DIP SW:2" )
	PORT_DIPSETTING(          0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, "Unused" ) PORT_DIPLOCATION( "DIP SW:3" )
	PORT_DIPSETTING(          0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, "Unused 2" ) PORT_DIPLOCATION( "DIP SW:4" )
	PORT_DIPSETTING(          0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00, DEF_STR( On ) )

INPUT_PORTS_END

ROM_START( gu956 )
	ROM_REGION( 0x80000, "flash", 0 )
	ROM_LOAD( "income_0000015.u7",   0x0000000, 0x080000, CRC(de6b63c2) SHA1(fa606a63042aac08d587a8896163e936372124b8) )
ROM_END


} // Anonymous namespace


// BIOS placeholder
GAME( 200?, gu956, 0,        gu956, gu956, gu956_state, empty_init, ROT0, "Konami", "GU956 Income System", MACHINE_IS_BIOS_ROOT )
