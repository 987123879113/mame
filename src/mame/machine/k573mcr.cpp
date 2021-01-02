// license:BSD-3-Clause
// copyright-holders:smf
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
	CN61           - BS8PSHF1AA 8 pin connector, connects MCR to memory card harness
	CN62           - BS8PSHF1AA 8 pin connector
	CN63           - 6P-SHVQ labeled "0", GE885-JB security dongle is connected here
	CN64           - 6P-SHVQ labeled "1"
	CN65           - B4PS-VH, 4 pin power connector
	CN67           - BS15PSHF1AA, 15-pin connector, unpopulated
	USB-A          - USB-A connector labeled 20J
	USB-B          - USB-B connector labeled 20L, connects MCR board to USB on System 573 motherboard
	ADM485JR       - Analog Devices ADM485 low power EIA RS-485 transceiver
	TMPR3904AF     - Toshiba TMPR3904AF RISC Microprocessor
	XCS05/10       - XILINX XCS10XL VQ100AKP9909 A2026631A
	EDO-DRAM4M-35  - Silicon Magic 66 MHz C9742 SM81C256K16CJ-35, 256K x 16 EDO DRAM
	EP4M16         - ROM labeled "855-A01"
 */

#include "emu.h"
#include "k573mcr.h"

k573mcr_device::k573mcr_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, KONAMI_573_MEMORY_CARD_READER, tag, owner, clock)
{
}

void k573mcr_device::device_start()
{
}

ROM_START( k573mcr )
	ROM_REGION( 0x080000, "tmpr3904", 0 )
	ROM_LOAD( "885a01.bin",   0x000000, 0x080000, CRC(e22d093f) SHA1(927f62f63b5caa7899392decacd12fea0e6fdbea) )
ROM_END

const tiny_rom_entry *k573mcr_device::device_rom_region() const
{
	return ROM_NAME( k573mcr );
}

DEFINE_DEVICE_TYPE(KONAMI_573_MEMORY_CARD_READER, k573mcr_device, "k573mcr", "Konami 573 Memory Card Reader")
