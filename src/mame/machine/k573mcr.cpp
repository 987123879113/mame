// license:BSD-3-Clause
// copyright-holders:smf, windyfairy
/*
 * Konami 573 Memory Card Reader

  Main PCB Layout
  ---------------
  GE885-PWB(A)A
  (C)1999 KONAMI CO. LTD.
  |---------------------------------------|
  |   PQ30RV11                            |
  |         |----------|  |-------|  CN61 |
  | DIP8    |          |  | XCS05 |       |
  |         |   TMPR   |  | /10   |       |
  |         |  3904AF  |  |-------|  CN62 |
  |         |          |                  |
  | CN65    |----------|                  |
  |        8.25 MHz                  CN67 |
  |                       EP4M16          |
  |             DRAM4M                    |
  |                                       |
  |                               --------|
  | USB-A                    CN64 |
  |                               |
  |          ADM485JR             |
  | USB-B                    CN63 |
  |-------------------------------|

Notes:
	DIP8       - 8-position DIP switch
	CN61       - BS8PSHF1AA 8 pin connector, connects to memory card harness
	CN62       - BS8PSHF1AA 8 pin connector
	CN63       - 6P-SHVQ labeled "0", GE885-JB security dongle is connected here
	CN64       - 6P-SHVQ labeled "1"
	CN65       - B4PS-VH, 4 pin power connector
	CN67       - BS15PSHF1AA, 15-pin connector, unpopulated
	USB-A      - USB-A connector
	USB-B      - USB-B connector, connects to USB on System 573 motherboard
	ADM485JR   - Analog Devices ADM485 low power EIA RS-485 transceiver
	TMPR3904AF - Toshiba TMPR3904AF RISC Microprocessor
	XCS05/10   - XILINX XCS10XL VQ100AKP9909 A2026631A
	DRAM4M     - Silicon Magic 66 MHz C9742 SM81C256K16CJ-35, 256K x 16 EDO DRAM
	EP4M16     - ROM labeled "855-A01"
*/

#include "emu.h"
#include "k573mcr.h"

k573mcr_device::k573mcr_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	jvs_device(mconfig, KONAMI_573_MEMORY_CARD_READER, tag, owner, clock)
{
}

void k573mcr_device::device_start()
{
	jvs_device::device_start();
}

void k573mcr_device::device_reset()
{
	jvs_device::device_reset();
	memset(pcb_buf, 0, 512);
	pcb_buf_addr = 0;
	pcb_port = 0;
	card1_status = card2_status = 0x0008;
	card3_status = 0;
	sec_plate_status = 0;
}

// void k573mcr_device::device_add_mconfig(machine_config &config)
// {
// 	TX3904BE(config, m_maincpu, 8.25_MHz_XTAL);
// 	m_maincpu->set_icache_size(4096);
// 	m_maincpu->set_dcache_size(1024);
// 	m_maincpu->set_addrmap(AS_PROGRAM, &k573mcr_device::amap);
// }

// void k573mcr_device::amap(address_map &map)
// {
// 	map(0x1fc00000, 0x1fc7ffff).rom().region("tmpr3904", 0);
// }

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

int k573mcr_device::handle_message_callback(const uint8_t *send_buffer, uint32_t send_size, uint8_t *&recv_buffer)
{
	// Returns the size of the parsed message, not the size of the response message

	switch(send_buffer[0]) {
	case 0xf0:
		jvs_address = 0xff;
		return -1;

	case 0x70:
		if (send_buffer[1] == 1) {
			// Buffer write
			// e0 01 6f 70 01 01 97 80 68 06 09 9f 1b 39 d3 44
			// 27 1f f5 00 57 df 40 7b 77 8b a6 70 09 06 b0 17
			// 04 b1 b1 46 30 90 8d 80 a9 48 da 16 f5 7f bf 0b
			// a7 36 43 46 0f 01 2f fe 15 a2 e0 1b 35 d7 3f d1
			// 06 2e 91 c9 bd f7 77 36 ea 97 37 2e b1 17 72 03
			// 4b d1 d0 59 43 0d f7 f7 df 90 dc f0 91 fe d0 0c
			// c0 f2 91 0f f7 77 3f f7 1e 63 1d 7f 6b f2 ff 00
			// 00 47
			memset(pcb_buf, 0x00, 512);
			memcpy(pcb_buf, send_buffer + 3, send_size - 3);

			*recv_buffer++ = 0x01;
			*recv_buffer++ = 0x01;
			return send_size;
		} else if (send_buffer[1] == 2) {
			// Sent after writing the firmware
			// e0 01 06 70 02 01 c0 00 3a 06
			*recv_buffer++ = 0x01;
			return 6;
		}

		printf("Unknown command!! 0x70 %02x\n", send_buffer[1]);
		return 0;

	case 0x73:
		// Firmware finished?
		// e0 01 02 73 76 05
		*recv_buffer++ = 0x01;
		*recv_buffer++ = 0x00;
		return 1;

	case 0x71:
	{
		// Get requested status
		// e0 01 02 71 74 00
		int status = sec_plate_status;

		/*
		if (pcb_port == 0) {
			status |= card1_status;
		} else if (pcb_port == 1) {
			status |= card2_status;
		} else if (pcb_port == 2) {
			// Card 3???
			status |= card3_status;
		}
		*/

		*recv_buffer++ = 0x01;
		*recv_buffer++ = status >> 8;
		*recv_buffer++ = status & 0xff;

		return 1;
	}

	case 0x76:
	{
		// memory card
		if (send_buffer[1] == 0x74) {
			// Read from card
			pcb_port = (send_buffer[2] & 0xf0) ? 1 : 0;
			pcb_buf_addr = ((send_buffer[2] << 8) | send_buffer[3]) & 0xfff;
			card1_status = 0x0008; // Not inserted
			card2_status = 0x0008; // Not inserted
			return 9;
		} else if (send_buffer[1] == 0x75) {
			// Write to card
			card1_status = 0x0000; // Failed to write
			card2_status = 0x0000; // Failed to write
		}

		printf("Unknown command!! 0x76 %02x\n", send_buffer[1]);
		return 0;
	}

	case 0x72:
		*recv_buffer++ = 0x01;
		return 1;
	}

	return -1;
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
