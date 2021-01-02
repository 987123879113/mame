// license:BSD-3-Clause
// copyright-holders:smf, windyfairy
/*
 * Konami 573 Memory Card Reader

  Main PCB Layout
  ---------------
  GE885-PWB(A)A
  (C)1999 KONAMI CO. LTD.
  |---------------------------------------------------------|
  |     PQ30RV11                                            |
  |                                  |----------|           |
  |  DIP8          |--------------|  |          |      CN61 |
  |                |              |  | XCS05/10 |           |
  |                |              |  |          |           |
  |                |  TMPR3904AF  |  |----------|           |
  |                |              |                    CN62 |
  |                |              |                         |
  |  CN65          |--------------|                         |
  |            8.25 MHz                                CN67 |
  |                                                         |
  |                                                         |
  |                                     EP4M16              |
  |                                                         |
  |                                                         |
  |                EDO-DRAM4M-35                            |
  |                                                         |
  |                                                         |
  |                                                 --------|
  |                                                 |
  |                                                 |
  | USB-A                                      CN64 |
  |                                                 |
  |          ADM485JR                               |
  |                                                 |
  | USB-B                                      CN63 |
  |                                                 |
  |-------------------------------------------------|

Notes:
	DIP8           - 8-position DIP switch
	CN61           - BS8PSHF1AA 8 pin connector, connects to memory card harness
	CN62           - BS8PSHF1AA 8 pin connector
	CN63           - 6P-SHVQ labeled "0", GE885-JB security dongle is connected here
	CN64           - 6P-SHVQ labeled "1"
	CN65           - B4PS-VH, 4 pin power connector
	CN67           - BS15PSHF1AA, 15-pin connector, unpopulated
	USB-A          - USB-A connector
	USB-B          - USB-B connector, connects to USB on System 573 motherboard
	ADM485JR       - Analog Devices ADM485 low power EIA RS-485 transceiver
	TMPR3904AF     - Toshiba TMPR3904AF RISC Microprocessor
	XCS05/10       - XILINX XCS10XL VQ100AKP9909 A2026631A
	EDO-DRAM4M-35  - Silicon Magic 66 MHz C9742 SM81C256K16CJ-35, 256K x 16 EDO DRAM
	EP4M16         - ROM labeled "855-A01"
 */

#include "emu.h"
#include "k573mcr.h"

k573mcr_device::k573mcr_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	jvs_device(mconfig, KONAMI_573_MEMORY_CARD_READER, tag, owner, clock)
	, m_maincpu(*this, "tmpr3904")
{
}

void k573mcr_device::device_start()
{
	jvs_device::device_start();
}

void k573mcr_device::device_reset()
{
	jvs_device::device_reset();
}

void k573mcr_device::device_add_mconfig(machine_config &config)
{
	TX3904BE(config, m_maincpu, 8.25_MHz_XTAL);
	m_maincpu->set_icache_size(4096);
	m_maincpu->set_dcache_size(1024);
	m_maincpu->set_addrmap(AS_PROGRAM, &k573mcr_device::amap);
}

void k573mcr_device::amap(address_map &map)
{
	map(0x00000000, 0x007fffff).ram(); // TODO: Find out proper location
	map(0x1fc00000, 0x1fc7ffff).rom().region("tmpr3904", 0);
}

const char *k573mcr_device::device_id()
{
	return "KONAMI CO.,LTD.;White I/O;Ver1.0;White I/O PCB";
}

uint8_t k573mcr_device::command_format_version()
{
	return 0x11;
}

uint8_t k573mcr_device::jvs_standard_version()
{
	return 0x20;
}

uint8_t k573mcr_device::comm_method_version()
{
	return 0x10;
}

ROM_START( k573mcr )
	ROM_REGION16_BE( 0x080000, "tmpr3904", 0 )
	ROM_LOAD16_WORD_SWAP( "885a01.bin",   0x000000, 0x080000, CRC(e22d093f) SHA1(927f62f63b5caa7899392decacd12fea0e6fdbea) )
ROM_END

const tiny_rom_entry *k573mcr_device::device_rom_region() const
{
	return ROM_NAME( k573mcr );
}

DEFINE_DEVICE_TYPE(KONAMI_573_MEMORY_CARD_READER, k573mcr_device, "k573mcr", "Konami 573 Memory Card Reader")
