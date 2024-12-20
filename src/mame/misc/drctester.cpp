// license:BSD-3-Clause
// copyright-holders:windyfairy

#include "emu.h"
#include "cpu/drctester/drctester.h"

namespace
{

class drctester_state : public driver_device
{
public:
	drctester_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
	{
	}

	void drctester(machine_config &config);

protected:
	required_device <drctester_cpu_device> m_maincpu;
};

void drctester_state::drctester(machine_config &config)
{
	DRCTESTER(config, m_maincpu, XTAL(1'000'000));
}

static INPUT_PORTS_START( drctester )
INPUT_PORTS_END

ROM_START( drctester )
	ROM_REGION( 4, "maincpu", 0 )
	ROM_LOAD( "test", 0, 4, NO_DUMP )
ROM_END

}

GAME( 2024, drctester, 0,  drctester,  drctester, drctester_state, empty_init, ROT0, "MAME", "DRC Tester", MACHINE_IS_BIOS_ROOT)
