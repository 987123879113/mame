// license:BSD-3-Clause
// copyright-holders:smf
/*
 * Konami 573 Multi Session Unit
 *
 */

#include "emu.h"
#include "k573msu.h"

/*

  PCB Layout of External Multisession Box
  ---------------------------------------

  GXA25-PWB(A)(C)2000 KONAMI
  |--------------------------------------------------------------------------|
  |CN9  ADM232  LS273        PC16552          PC16552         XC9536(1)  CN13|
  |DSW(8)  LS245   LS273            18.432MHz                        DS2401  |
  |         |-------|      |-------|       |-------|      |-------|          |
  | MB3793  |TOSHIBA|      |TOSHIBA|       |TOSHIBA|      |TOSHIBA|M48T58Y.6T|
  |         |TC9446F|      |TC9446F|       |TC9446F|      |TC9446F|          |
  |         |-016   |      |-016   |       |-016   |      |-016   |      CN12|
  |         |-------|      |-------|       |-------|      |-------|          |
  |       LV14                    XC9572XL                                   |
  | CN16                 CN17                 CN18             CN19 XC9536(2)|
  |PQ30RV21        LCX245   LCX245                                       CN11|
  |                                  33.8688MHz              PQ30RV21        |
  |    8.25MHz   HY57V641620                                                 |
  |  |------------|     HY57V641620   XC2S200                                |
  |  |TOSHIBA     |                                          FLASH.20T       |
  |  |TMPR3927AF  |                                                      CN10|
  |  |            |                                                          |
  |  |            |                                     LS245   F245  F245   |
  |  |            |HY57V641620  LCX245     DIP40                             |
  |  |------------|     HY57V641620  LCX245                   ATAPI44        |
  |                             LCX245              LED(HDD)  ATAPI40        |
  |    CN7                      LCX245      CN14    LED(CD)           CN5    |
  |--------------------------------------------------------------------------|
  Notes: (all IC's shown)
          TMPR3927     - Toshiba TMPR3927AF Risc Microprocessor (QFP240)
          FLASH.20T    - Fujitsu 29F400TC Flash ROM (TSOP48)
          ATAPI44      - IDE44 44-pin laptop type HDD connector (not used)
          ATAPI40      - IDE40 40-pin flat cable HDD connector used for connection of CDROM drive
          XC9572XL     - XILINX XC9572XL In-system Programmable CPLD stamped 'XA25A1' (TQFP100)
          XC9536(1)    - XILINX CPLD stamped 'XA25A3' (PLCC44)
          XC9536(2)    - XILINX CPLD stamped 'XA25A2' (PLCC44)
          XC2S200      - XILINX XC2S200 SPARTAN FPGA (QFP208)
          DS2401       - MAXIM Dallas DS2401 Silicon Serial Number (SOIC6)
          M48T58Y      - ST M48T58Y Timekeeper NVRAM 8k bytes x8-bit (DIP28). Chip appears empty (0x04 fill) or unused
          MB3793       - Fujitsu MB3793 Power-Voltage Monitoring IC with Watchdog Timer (SOIC8)
          DIP40        - Empty DIP40 socket
          HY57V641620  - Hyundai/Hynix HY57V641620 4 Banks x 1M x 16Bit Synchronous DRAM
          PC16552D     - National PC16552D Dual Universal Asynchronous Receiver/Transmitter with FIFO's
          TC9446F      - Toshiba TC9446F-016 Audio Digital Processor for Decode of Dolby Digital (AC-3) MPEG2 Audio
          CN16-CN19    - Connector for sub board (3 of them are present). One board connects via a thin cable from
                         CN1 to the main board to a connector on the security board labelled 'AMP BOX'.

  Sub Board Layout
  ----------------

  GXA25-PWB(B) (C) 2000 KONAMI
  |---------------------------------|
  | TLP2630  LV14          ADM232   |
  |CN2                           CN1|
  |A2430         AK5330             |
  |                                 |
  |                          RCA L/R|
  |ZUS1R50505        6379A          |
  |                          LM358  |
  |---------------------------------|

*/

k573msu_device::k573msu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, KONAMI_573_MULTI_SESSION_UNIT, tag, owner, clock),
  m_maincpu(*this, "maincpu"),
  m_dsp(*this, "dsp_%d", 0L)
{
}

void k573msu_device::device_add_mconfig(machine_config &config)
{
  TX3927(config, m_maincpu, 25_MHz_XTAL); // TODO: Fix clock
  // m_maincpu->set_addrmap(AS_PROGRAM, &k573msu_device::amap);
  m_maincpu->in_brcond<0>().set([]() { return 1; }); // writeback complete
  m_maincpu->in_brcond<1>().set([]() { return 1; }); // writeback complete
  m_maincpu->in_brcond<2>().set([]() { return 1; }); // writeback complete
  m_maincpu->in_brcond<3>().set([]() { return 1; }); // writeback complete

	auto& dsp0(TC9446F(config, "dsp_0", 0));
	// dsp0.mpeg_frame_sync_cb().set(*this, FUNC(k573msu_device::mpeg_frame_sync<0>));
	// dsp0.demand_cb().set(*this, FUNC(k573msu_device::audio_demand<0>));
	dsp0.add_route(0, ":lspeaker", 0.65);
	dsp0.add_route(1, ":rspeaker", 0.65);

	auto& dsp1(TC9446F(config, "dsp_1", 0));
	// dsp1.mpeg_frame_sync_cb().set(*this, FUNC(k573msu_device::mpeg_frame_sync<1>));
	// dsp1.demand_cb().set(*this, FUNC(k573msu_device::audio_demand<1>));
	dsp1.add_route(0, ":lspeaker", 0.65);
	dsp1.add_route(1, ":rspeaker", 0.65);

	auto& dsp2(TC9446F(config, "dsp_2", 0));
	// dsp2.mpeg_frame_sync_cb().set(*this, FUNC(k573msu_device::mpeg_frame_sync<2>));
	// dsp2.demand_cb().set(*this, FUNC(k573msu_device::audio_demand<2>));
	dsp2.add_route(0, ":lspeaker", 0.65);
	dsp2.add_route(1, ":rspeaker", 0.65);

	auto& dsp3(TC9446F(config, "dsp_3", 0));
	// dsp3.mpeg_frame_sync_cb().set(*this, FUNC(k573msu_device::mpeg_frame_sync<3>));
	// dsp3.demand_cb().set(*this, FUNC(k573msu_device::audio_demand<3>));
	dsp3.add_route(0, ":lspeaker", 0.65);
	dsp3.add_route(1, ":rspeaker", 0.65);
}

void k573msu_device::device_start()
{
}

ROM_START( k573msu )
	ROM_REGION( 0x080000, "tmpr3927", 0 )
	ROM_LOAD( "flash.20t",    0x000000, 0x080000, CRC(b70c65b0) SHA1(d3b2bf9d3f8b1caf70755a0d7fa50ef8bbd758b8) ) // from "GXA25-PWB(A)(C)2000 KONAMI"

	ROM_REGION( 0x002000, "m48t58y", 0 )
	ROM_LOAD( "m48t58y.6t",   0x000000, 0x002000, CRC(609ef020) SHA1(71b87c8b25b9613b4d4511c53d0a3a3aacf1499d) )
ROM_END

const tiny_rom_entry *k573msu_device::device_rom_region() const
{
	return ROM_NAME( k573msu );
}

DEFINE_DEVICE_TYPE(KONAMI_573_MULTI_SESSION_UNIT, k573msu_device, "k573msu", "Konami 573 Multi Session Unit")
