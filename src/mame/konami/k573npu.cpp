// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 Network PCB Unit
 *
 */

#include <iostream>

#include "emu.h"
#include "k573npu.h"

#define LOG_GENERAL    (1 << 0)
#define LOG_FPGA       (1 << 1)

#define VERBOSE        (LOG_GENERAL)
#define LOG_OUTPUT_STREAM std::cout

#include "logmacro.h"

/*

  System 573 Hard Drive and Network Unit
  --------------------------------------

  This box is used with later Drum Mania and Guitar Freaks (possibly 9 to 11)

  PCB Layout
  ----------

  PWB0000100991 (C)2001 KONAMI
  |--------------------------------------------------------------------------|
  |    CN1               MB3793     74HC14          FLASH.24E       RJ45     |
  |                                                                          |
  |    LCX245                               DIP40                         CN3|
  |LCX245 LCX245|-------|                                   PE68515L         |
  |             |       | DS2401                          |--------|  SP232  |
  |PQ30RV21     |XC2S100|           XC9572XL              |NATIONAL|  25MHz  |
  |             |       |                                 |DP83815 |   93LC46|
  |             |-------|                                 |        |         |
  |          74LS245 74LS245                              |--------|        L|
  |PQ30RV21            74LS245 74LS245                                      L|
  |         IDE44   HDD_LED          LCX245 LCX245 LCX245           DIPSW(8)L|
  |---------------------------------|   LCX245  LCX245                      L|
                                    |                                       L|
                                    |                              74LS273  L|
                                    |                                       L|
                                    |   48LC4M16  |------------|            L|
                                    |             |TOSHIBA     |             |
                                    |             |TMPR3927CF  |             |
                                    |             |            |   74LS245   |
                                    |             |            |             |
                                    |             |            |             |
                                    |   48LC4M16  |------------|             |
                                    |                                        |
                                    |                8.28MHz              CN2|
                                    |                                        |
                                    |----------------------------------------|
  Notes: (all IC's shown)
        TMPR3927 - Toshiba TMPR3927CF Risc Microprocessor (QFP240)
        FLASH    - Fujitsu 29F400TC Flash ROM (TSOP48)
        IDE44    - IDE44 44-pin laptop type HDD connector. The Hard Drive connected is a
                   2.5" Fujitsu MHR2010AT 10GB HDD with Konami sticker C07JAA03
        48LC4M16 - Micron Technology 48LC4M16 4M x16-bit SDRAM (TSSOP54)
        XC9572XL - XILINX XC9572XL In-system Programmable CPLD stamped 'UC07A1' (TQFP100)
        XC2S100  - XILINX XC2S100 SPARTAN-II 2.5V FPGA (TQFP144)
        DS2401   - MAXIM Dallas DS2401 Silicon Serial Number (SOIC6)
        93LC46   - 128 bytes x8-bit EEPROM (SOIC8)
        MB3793   - Fujitsu MB3793 Power-Voltage Monitoring IC with Watchdog Timer (SOIC8)
        PE68515L - Pulse PE-68515L 10/100 Base-T Single Port Transformer Module
        DP83815  - National Semiconductor DP83815 10/100 Mb/s Integrated PCI Ethernet Media
                   Access Controller and Physical Layer (TQFP144)
        SP232    - Sipex Corporation SP232 Enhanced RS-232 Line Drivers/Receiver (SOIC16)
        RJ45     - RJ45 network connector
        DIP40    - Empty DIP40 socket
        CN1      - 68-pin VHDCI connector. Uses a VHDCI to VHDCI cable to connect with the main Sys573 via a PCMCIA card that has a VHDCI connector on the end.
        CN2      - 6-pin power input connector
        CN3      - 4-pin connector
        L        - LED

  The related PCMCIA card that is inserted into the System 573 and is used as the connection point between the NPU and Sys573 is a simple passthrough card
  with 2 capacitors on the VHDCI side of the board and 6 ferrite bead chips (ZBDS5101-8PT).
  Card is marked "K5010-2501 Ver 1.1 CARD-BUS".

*/

k573npu_device::k573npu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, KONAMI_573_NETWORK_PCB_UNIT, tag, owner, clock),
	digital_id(*this, "digital_id"),
	m_maincpu(*this, "maincpu"),
	m_ram(*this, "ram")
{
}

void k573npu_device::device_start()
{
}

void k573npu_device::device_reset()
{
}

void k573npu_device::device_add_mconfig(machine_config& config)
{
	RAM(config, m_ram).set_default_size("32M").set_default_value(0);

	TX3927(config, m_maincpu, 20_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &k573npu_device::amap);
	m_maincpu->in_brcond<0>().set([]() { return 1; }); // writeback complete
	m_maincpu->in_brcond<1>().set([]() { return 1; }); // writeback complete
	m_maincpu->in_brcond<2>().set([]() { return 1; }); // writeback complete
	m_maincpu->in_brcond<3>().set([]() { return 1; }); // writeback complete

	DS2401(config, digital_id);

	//pci_bus_legacy_device& pcibus(PCI_BUS_LEGACY(config, "pcibus", 0, 0));
}

uint16_t k573npu_device::fpgasoft_read(offs_t offset, uint16_t mem_mask)
{
	if (offset * 2 != 0x20) {
		LOGMASKED(LOG_GENERAL, "%s: fpgasoft_read %08x %08x\n", machine().describe_context().c_str(), offset * 2, mem_mask);
	}

	switch (offset * 2) {
	case 0x20:
		// Communication with Dallas serial
		return digital_id->read();

	case 0x24:
		// Version
		return 1;

	case 0x26:
		// Xilinx FPGA version?
		// 5963 = XC9536?
		return 0x5963;
	}

	return 0;
}

void k573npu_device::fpgasoft_write(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_GENERAL, "%s: fpgasoft_write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 2, data, mem_mask);

	switch (offset * 2) {
	case 0x20:
		// Communication with Dallas serial
		digital_id->write(data & 1);
		break;
	}
}

uint16_t k573npu_device::fpga_read(offs_t offset, uint16_t mem_mask)
{
	if (offset == 1)
		return 3;

	LOGMASKED(LOG_FPGA, "%s: fpga_read %08x %08x\n", machine().describe_context().c_str(), offset * 2, mem_mask);

	return 0;
}

void k573npu_device::fpga_write(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (offset != 4)
		LOGMASKED(LOG_FPGA, "%s: fpga_write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 2, data, mem_mask);
}


void k573npu_device::amap(address_map& map)
{
	map(0x00000000, 0x0fffffff).ram().share("ram");
	map(0x80000000, 0x8fffffff).ram().share("ram");
	map(0xa0000000, 0xafffffff).ram().share("ram");

	map(0x10200000, 0x1020000f).rw(FUNC(k573npu_device::fpga_read), FUNC(k573npu_device::fpga_write));
	map(0x10400000, 0x10400fff).rw(FUNC(k573npu_device::fpgasoft_read), FUNC(k573npu_device::fpgasoft_write));

	map(0x1fc00000, 0x1fc7ffff).rom().region("tmpr3927", 0);
}

ROM_START( k573npu )
	ROM_REGION32_BE( 0x080000, "tmpr3927", 0 )
	ROM_LOAD16_WORD_SWAP( "29f400.24e",   0x000000, 0x080000, CRC(8dcf294b) SHA1(efac79e18db22c30886463ec1bc448187da7a95a) )

	ROM_REGION( 0x000008, "digital_id", 0 )
	ROM_LOAD( "digital-id.bin",   0x000000, 0x000008, CRC(2b977f4d) SHA1(2b108a56653f91cb3351718c45dfcf979bc35ef1) )
ROM_END

const tiny_rom_entry *k573npu_device::device_rom_region() const
{
	return ROM_NAME( k573npu );
}

DEFINE_DEVICE_TYPE(KONAMI_573_NETWORK_PCB_UNIT, k573npu_device, "k573npu", "Konami 573 Network PCB Unit")
