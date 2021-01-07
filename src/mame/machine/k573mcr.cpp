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
	memset(pcb_buf, 0, 65535);
	pcb_buf_addr = 0;
	controller_port = 0;
	sec_slot = 0;
	card_status[0] = card_status[1] = 0;
	is_controller_connected[0] = is_controller_connected[1] = false;
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

int k573mcr_device::handle_message_callback(const uint8_t *send_buffer, uint32_t send_size, uint8_t *&recv_buffer)
{
	// Returns the size of the parsed message, not the size of the response message
	// TODO: Fix the jvs interface to allow for null responses (0x70 0x01 returns a null response for buffer writes)

	switch(send_buffer[0]) {
		case 0xf0:
		{
			// Hack but I haven't looked into why this hack is required to pass init
			// TODO: Fix hack or find justification for it
			jvs_address = 0xff;
			return -1;
		}

		case 0x70:
		{
			if (send_buffer[1] == 0) {
				// Buffer read
				// e0 01 07 70 00 02 00 00 05 7f
				int target_offset = ((send_buffer[3] << 8) | send_buffer[4]) & 0x7fff;
				int target_len = send_buffer[5];

				*recv_buffer++ = 0x01;

				for (int i = 0; i < target_len && i + target_offset < 65535; i++) {
					*recv_buffer++ = pcb_buf[target_offset + i];
				}

				return 6;
			} else if (send_buffer[1] == 1) {
				// Buffer write
				// e0 01 6f 70 01 01 ...(payload)... 47
				if (send_size - 3 > 0) {
					memcpy(pcb_buf, send_buffer + 3, send_size - 3);
				}
				*recv_buffer++ = 0x01; // Hack because MAME does not support empty responses yet
				return send_size;
			} else if (send_buffer[1] == 2) {
				// Sent after writing the firmware
				// e0 01 06 70 02 01 c0 00 3a 06
				*recv_buffer++ = 0x01;
				return 6;
			}

			printf("Unknown command!! 0x70 (buf) %02x\n", send_buffer[1]);
			exit(1);

			return 0;
		}

		case 0x71:
		{
			// Status request
			// e0 01 02 71 74 00
			int status = card_status[controller_port];

			*recv_buffer++ = 0x01;
			*recv_buffer++ = status >> 8;
			*recv_buffer++ = status & 0xff;

			return 1;
		}

		case 0x72:
		{
			// Security plate
			uint8_t cmd = send_buffer[1] & ~1;
			sec_slot = send_buffer[1] & 1;

			if (cmd == 0x00) {
				// e0 01 03 72 00 76 slot 1
				// e0 01 03 72 01 77 slot 2
				*recv_buffer++ = 0x01;
				return 2;
			} else if (cmd == 0x10) {
				// Set password (presumably to read dongle)
				*recv_buffer++ = 0x01;
				return 10;
			} else if (cmd == 0x20) {
				// Corresponds to data from CN63 dongle
				// "GE885-JB" dongle.
				// A second dongle may exist for GE929
				// but I have not seen one.
				// Later games don't seem to check this.
				pcb_buf[0] = 0x4A; // J
				pcb_buf[1] = 0x42; // B
				pcb_buf[2] = 0x00;
				pcb_buf[3] = 0x00;

				pcb_buf[4] = ~(pcb_buf[0] + pcb_buf[1]); // Checksum byte

				*recv_buffer++ = 0x01;
				return 10;
			} else if (cmd == 0x40) {
				// "config register"
				// Some kind of registration info?
				// e0 01 06 72 40 02 00 00 bb 41
				pcb_buf[0] = 0xFF;
				pcb_buf[1] = 0xFF;
				pcb_buf[2] = 0xAC;
				pcb_buf[3] = 0x09;
				pcb_buf[4] = 0x00;
				*recv_buffer++ = 0x01;
				return 6;
			}

			printf("Unknown command!! 0x72 (sec plate) %02x\n", send_buffer[1]);
			exit(1);

			return 0;
		}

		case 0x73:
		{
			// Firmware finished?
			// e0 01 02 73 76 05
			*recv_buffer++ = 0x01;
			*recv_buffer++ = 0x01;
			return 1;
		}

		case 0x76:
		{
			// memory card
			if (send_buffer[1] == 0x74) {
				// Read from card
				// e0 01 0a 76 74 00 00 02 00 00 00 01 f8 39
				// e0 01 0a 76 74 80 00 02 00 00 00 01 78 39
				controller_port = send_buffer[2] >> 7;
				pcb_buf_addr = ((send_buffer[2] << 8) | send_buffer[3]) & 0x7fff;

				card_status[0] = 0x0008; // Not inserted
				card_status[1] = 0x0008; // Not inserted

				*recv_buffer++ = 0x01;

				return 10;
			} else if (send_buffer[1] == 0x75) {
				// Write to card
				card_status[0] = 0x0000; // Failed to write
				card_status[1] = 0x0000; // Failed to write
				*recv_buffer++ = 0x01;
			}

			printf("Unknown command!! 0x76 (mem card) %02x\n", send_buffer[1]);
			exit(1);

			return 0;
		}

		case 0x77:
		{
			// Playstation controller inputs
			// TODO: Why isn't this polled like memory cards? Is a flag missing?
			//
			// This was used in Guitar Freaks starting with GF 2nd Mix Link Kit 2
			// which allowed players to bring their own PS1 compatible guitars
			// to the arcade.
			// e0 01 02 77 7a

			// ref: psx_controller_port_device's PSXPAD0 and PSXPAD1 definitions
			// The only difference seems to be that 0x02 determines if the controller
			// is connected or not.
			uint8_t p1_psxpad0, p1_psxpad1, p2_psxpad0, p2_psxpad1;

			p1_psxpad0 = 0xff;
			p1_psxpad1 = 0xff;
			p2_psxpad0 = 0xff;
			p2_psxpad1 = 0xff;

			if (!is_controller_connected[0]) {
				p1_psxpad0 = 0;
				p1_psxpad1 = 0;
			}

			if (!is_controller_connected[1]) {
				p2_psxpad0 = 0;
				p2_psxpad1 = 0;
			}

			*recv_buffer++ = 0x01;
			*recv_buffer++ = p1_psxpad0; // P1 PSXPAD0
			*recv_buffer++ = p1_psxpad1; // P1 PSXPAD1
			*recv_buffer++ = p2_psxpad0; // P2 PSXPAD0
			*recv_buffer++ = p2_psxpad1; // P2 PSXPAD1
			*recv_buffer++ = 0x00; // ?
			return 2;
		}
	}

	if (send_buffer[0] > 0x70) {
		printf("Found unimplemented opcode: %02x\n", send_buffer[0]);
		exit(1);
	}

	// Command not recognized, pass it off to the base message handler
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
