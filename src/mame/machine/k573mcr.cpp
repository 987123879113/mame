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

// #define K573MCR_DEBUG

#include "emu.h"
#include "k573mcr.h"

k573mcr_device::k573mcr_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	jvs_device(mconfig, KONAMI_573_MEMORY_CARD_READER, tag, owner, clock),
	m_ports{{*this, "port1"}, {*this, "port2"}}
{
}

void k573mcr_device::device_start()
{
	jvs_device::device_start();

	save_item(NAME(m_ram));
	save_item(NAME(m_ram_addr));
	save_item(NAME(m_current_device));
	save_item(NAME(m_memcard_port));
	save_item(NAME(m_memcard_addr));
	save_item(NAME(m_memcard_status));
	save_item(NAME(m_sec_slot));
}

void k573mcr_device::device_reset()
{
	jvs_device::device_reset();

	memset(m_ram, 0, RAM_SIZE);
	m_ram_addr = 0;
	m_current_device = DEVICE_SELF;

	m_sec_slot = 0;
	m_memcard_port = m_memcard_addr = 0;
	m_memcard_status[0] = m_memcard_status[1] = MEMCARD_UNINITIALIZED;
}

void k573mcr_device::device_add_mconfig(machine_config &config)
{
	// The actual controllers are only used in Guitar Freaks and it was
	// meant to be used with the home version PS1 guitar controller.
	// The PS1 guitar controller isn't emulated in MAME and it doesn't
	// make much sense to have it enabled by default. You can still select
	// a controller through the slots menu.
	// The memory card ports are still usable even without a controller
	// enabled which is the main reason for using the PSX controller ports.
	PSX_CONTROLLER_PORT(config, m_ports[0], psx_controllers, nullptr);
	PSX_CONTROLLER_PORT(config, m_ports[1], psx_controllers, nullptr);
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

uint8_t k573mcr_device::controller_port_send_byte(uint32_t port_no, uint8_t data)
{
	auto port = m_ports[port_no];
	uint8_t output = 0;

	for (int i = 0; i < 8; i++) {
		port->clock_w(0);
		port->tx_w(!!(data & (1 << i)));
		port->clock_w(1);
		output |= port->rx_r() << i;
	}

	return output;
}

bool k573mcr_device::pad_read(uint32_t port_no, uint8_t *output)
{
	m_ports[port_no]->sel_w(1);
	m_ports[port_no]->sel_w(0);

	controller_port_send_byte(port_no, 0x01);
	uint8_t a = controller_port_send_byte(port_no, 'B');
	uint8_t b = controller_port_send_byte(port_no, 0);
	*output++ = controller_port_send_byte(port_no, 0);
	*output++ = controller_port_send_byte(port_no, 0);

	return a == 0x41 && b == 0x5a;
}

bool k573mcr_device::memcard_read(uint32_t port_no, uint16_t block_addr, uint8_t *output)
{
	m_ports[port_no]->sel_w(1);
	m_ports[port_no]->sel_w(0);

	controller_port_send_byte(port_no, 0x81);

	if (controller_port_send_byte(port_no, 'R') == 0xff) { // state_command, Request read
		return false;
	}

	controller_port_send_byte(port_no, 0); // state_command -> state_cmdack
	controller_port_send_byte(port_no, 0); // state_cmdack -> state_wait
	controller_port_send_byte(port_no, block_addr >> 8); // state_wait -> state_addr_hi
	controller_port_send_byte(port_no, block_addr & 0xff); // state_addr_hi -> state_addr_lo

	if (controller_port_send_byte(port_no, 0) != 0x5c) {  // state_addr_lo -> state_read
		// If the command wasn't correct then it transitions to state_illegal at this point
		return false;
	}

	controller_port_send_byte(port_no, 0); // Skip 0x5d
	controller_port_send_byte(port_no, 0); // Skip addr hi
	controller_port_send_byte(port_no, 0); // Skip addr lo

	for (int i = 0; i < 128; i++) {
		auto c = controller_port_send_byte(port_no, 0);
		if (output != nullptr) {
			*output++ = c;
		}
	}

	controller_port_send_byte(port_no, 0);

	return controller_port_send_byte(port_no, 0) == 'G';
}

bool k573mcr_device::memcard_write(uint32_t port_no, uint16_t block_addr, uint8_t *input)
{
	m_ports[port_no]->sel_w(1);
	m_ports[port_no]->sel_w(0);

	controller_port_send_byte(port_no, 0x81);

	if (controller_port_send_byte(port_no, 'W') == 0xff) { // state_command, Request write
		return false;
	}

	controller_port_send_byte(port_no, 0); // state_command -> state_cmdack
	controller_port_send_byte(port_no, 0); // state_cmdack -> state_wait
	controller_port_send_byte(port_no, block_addr >> 8); // state_wait -> state_addr_hi
	controller_port_send_byte(port_no, block_addr & 0xff); // state_addr_hi -> state_addr_lo

	uint8_t checksum = (block_addr >> 8) ^ (block_addr & 0xff);
	for (int i = 0; i < 128; i++) {
		controller_port_send_byte(port_no, input[i]); // state_read
    	checksum ^= input[i];
	}

	controller_port_send_byte(port_no, checksum);
	controller_port_send_byte(port_no, 0);
	controller_port_send_byte(port_no, 0);

	return controller_port_send_byte(port_no, 0) == 'G';
}

int k573mcr_device::device_handle_message(const uint8_t *send_buffer, uint32_t send_size, uint8_t *&recv_buffer)
{
	// Notes:
	// 80678::E0:01:06:70:02:01:C0:00:3A: <- Command from game (E0:01:...)
	// 80681::E0:00:03:01:01:05: <- Response from memory card reader device (E0:00:...)
	//
	// The returned value of this function should be 0 (invalid parameters), -1 (unknown command), or the number of bytes in the message.
	// The number of bytes to return is covered by the xx section:
	// 80678::E0:01:yy:xx:xx:xx:xx:xx:3A:
	// This should correspond to yy - 1, but you don't actually get access to yy in the message handler so you must calculate it yourself.
	//
	// recv_buffer will correspond to this part when returning data:
	// 80681::E0:00:03:01:rr:05:
	// In some special cases there is an empty respond but that is not supported in MAME currently so a single byte is returned instead.

	switch(send_buffer[0]) {
		case 0xf0:
			// The bootloader for System 573 games checks for the master calendar which initializes the JVS device.
			// After the bootloader, the actual game's code tries to initialize the JVS device again and (seemingly)
			// expects it to be in a fresh state. Since it was already initialized in the bootloader, it will throw
			// the error message "JVS SUBS RESET ERROR".
			// There might be something else that happens on real hardware between when it loads the bootloader
			// and when it starts the actual game's code that resets the JVS device, but I do not have hands on
			// access to test such a thing.
			// To hack around that error, set jvs_address back to 0xff whenever the reset command is called.
			jvs_address = 0xff;
			return -1;

		case 0x14:
			// Function list returns nothing on
			// 75502::E0:01:02:14:17:
			// 75503::E0:00:04:01:01:00:06:
			*recv_buffer++ = 0x01;
			*recv_buffer++ = 0x00;
			return 1;

		case 0x70:
		{
			int target_len = send_buffer[5];
			m_ram_addr = ((send_buffer[3] << 8) | send_buffer[4]) & 0x7fff;

			if (send_buffer[1] == 0) {
				// Buffer read
				// Real packet capture, does *not* include a checksum at the end
				// 39595::E0:01:07:70:00:02:00:00:80:FA:
				// 39596::E0:00:83:01:01:4D:43:00:00:00:00:00:00:00:00:00
				//       :00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00
				//       :00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00
				//       :00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00
				//       :00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00
				//       :00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00
				//       :00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00
				//       :00:00:00:00:00:00:
#ifdef K573MCR_DEBUG
				printf("jvs buf read: %04x %04x, mode = %d\n", m_ram_addr, target_len, m_current_device);
				printf("\t");
				for (int i = 0; i < 6; i++) {
					printf("%02x ", send_buffer[i]);
				}
				printf("\n\n");
#endif

				*recv_buffer++ = 0x01;

				if (m_current_device == DEVICE_MEMORY_CARD && m_memcard_status[m_memcard_port] == MEMCARD_AVAILABLE) {
					memcard_read(m_memcard_port, m_memcard_addr + m_ram_addr / MEMCARD_BLOCK_SIZE, m_ram + m_ram_addr);
				}

				for (int i = 0; i < target_len && i + m_ram_addr < RAM_SIZE; i++) {
					*recv_buffer++ = m_ram[m_ram_addr + i];
				}

				return 6;
			} else if (send_buffer[1] == 1) {
				// Buffer write
				// Real packet
				// 77524::E0:01:87:70:01:02:03:00:80:00:1A:ED:00:FF:85:38
				//       :21:00:07:38:42:3C:EF:05:00:FF:34:00:AC:00:40:30
				//       :F9:21:02:18:41:09:46:10:23:00:45:10:E2:1C:FF:1C
				//       :42:8E:FD:2C:04:E8:C0:25:FF:04:02:5D:31:60:D0:AF
				//       :00:00:F7:00:88:46:
				// 77544::E0:01:
				//
				// Real packet
				// 77545::E0:01:87:70:01:02:03:80:80:12:FB:00:0C:02:3C:50
				//       :5E:D0:CF:8E:04:FF:00:00:8E:05:00:04:8E:06:DF:00
				//       :08:02:12:80:00:98:03:43:FF:26:31:00:22:EF:40:41
				//       :00:EC:8F:8F:8F:D1:4C:20:10:24:7C:70:87:00:00:03
				//       :AC:42:00:31:06:01:
				// 77563::E0:03:05:
				//
				// Real packet
				// 77565::E0:01:87:70:01:02:04:00:80:24:A5:04:70:30:30:7A
				//       :24:87:C6:85:10:00:64:05:58:02:44:45:CA:8C:FD:A2
				//       :00:04:A3:00:04:8C:A6:00:FF:08:8C:00:82:00:20:00
				//       :E8:00:F4:00:DD:F8:C0:24:EF:85:FC:21:20:20:04:5B
				//       :14:40:51:73:77:
				// 77583::E0:00:01:
				if (target_len > 0) {
					memcpy(m_ram + m_ram_addr, send_buffer + 6, target_len);
				}

				*recv_buffer++ = 0x01;

				return 6 + target_len;
			} else if (send_buffer[1] == 2) {
				// Sent after writing the firmware
				// 80678::E0:01:06:70:02:01:C0:00:3A:
				// 80681::E0:00:03:01:01:05:
				*recv_buffer++ = 0x01;

				return 5;
			}

#ifdef K573MCR_DEBUG
			printf("Unknown command!! 0x70 (buf) %02x\n", send_buffer[1]);
			exit(1);
#endif

			return -1;
		}

		case 0x71:
		{
			// Status request
			// 81681::E0:01:02:71:74:
			// 81682::E0:00:05:01:01:00:00:07:
			int status = m_memcard_status[m_memcard_port];

			*recv_buffer++ = 0x01;
			*recv_buffer++ = status >> 8;
			*recv_buffer++ = status & 0xff;

#ifdef K573MCR_DEBUG
			printf("jvs status %d %04x\n", m_memcard_port, status);
#endif

			if (m_memcard_status[m_memcard_port] == MEMCARD_UNINITIALIZED || m_memcard_status[m_memcard_port] == MEMCARD_UNAVAILABLE) {
				m_memcard_status[m_memcard_port] = memcard_read(m_memcard_port, 0, nullptr) ? MEMCARD_AVAILABLE : MEMCARD_UNAVAILABLE;
			}

			if (m_memcard_status[m_memcard_port] & MEMCARD_READING) {
				// Real device packets
				// 39542::E0:01:02:71:74:
				// 39543::E0:00:05:01:01:02:00:09:
				// 39578::E0:01:02:71:74:
				// 39579::E0:00:05:01:01:80:00:87:
				if (memcard_read(m_memcard_port, m_memcard_addr, nullptr)) {
					m_memcard_status[m_memcard_port] = MEMCARD_AVAILABLE;
				} else {
					m_memcard_status[m_memcard_port] = MEMCARD_UNAVAILABLE;
				}
			} else if (m_memcard_status[m_memcard_port] & MEMCARD_WRITING) {
				// Real device packets
				// 39358::E0:01:02:71:74:
				// 39359::E0:00:05:01:01:04:00:0B:
				// 39394::E0:01:02:71:74:
				// 39395::E0:00:05:01:01:00:00:07:
				if (memcard_read(m_memcard_port, m_memcard_addr, nullptr)) {
					m_memcard_status[m_memcard_port] = MEMCARD_UNINITIALIZED;
				} else {
					m_memcard_status[m_memcard_port] = MEMCARD_UNAVAILABLE;
				}
			} else if (m_memcard_status[m_memcard_port] == MEMCARD_ERROR) {
				m_memcard_status[m_memcard_port] = MEMCARD_UNAVAILABLE;
			}

			return 1;
		}

		case 0x72:
		{
			// Security plate
			uint8_t cmd = send_buffer[1] & ~1;
			m_sec_slot = send_buffer[1] & 1;

			if (cmd == 0x00) {
				// Packet: e0 01 03 72 00 76 slot 0 (CN63)
				// Packet: e0 01 03 72 01 77 slot 1 (CN64)
				*recv_buffer++ = 0x01;

				return 2;
			} else if (cmd == 0x10) {
				// Set password (presumably to unlock/read security dongle)
				// Packet: e0 01 0b 72 10 a4 60 f0 5d ea c4 5d ec d6
				*recv_buffer++ = 0x01;

				return 10;
			} else if (cmd == 0x20) {
				// Get dongle data? (slot 0: GE885-JB, slot 1: ?)
				// Packet: e0 01 0a 72 20 00 00 02 00 00 00 08 a7
				// It seems that as long as the checksum matches, the game will accept it as valid
				m_ram[0] = 'J';
				m_ram[1] = 'B';
				m_ram[2] = 0x00;
				m_ram[3] = 0x00;
				m_ram[4] = ~(m_ram[0] + m_ram[1]); // Checksum byte

				m_current_device = DEVICE_SECURITY_PLATE;

				*recv_buffer++ = 0x01;

				return 9;
			} else if (cmd == 0x40) {
				// Get some kind of registration info from dongle?
				// Game code calls it "config register"
				// Packet: e0 01 06 72 40 02 00 00 bb
				m_ram[0] = 0xFF;
				m_ram[1] = 0xFF;
				m_ram[2] = 0xAC;
				m_ram[3] = 0x09;
				m_ram[4] = 0x00;

				m_current_device = DEVICE_SECURITY_PLATE;

				*recv_buffer++ = 0x01;

				return 5;
			}

#ifdef K573MCR_DEBUG
			printf("Unknown command!! 0x72 (sec plate) %02x\n", send_buffer[1]);
			exit(1);
#endif

			return -1;
		}

		case 0x73:
		{
			// Firmware finished?
			// 81674::E0:01:02:73:76:
			// 81675::E0:00:03:01:01:05:
			*recv_buffer++ = 0x01;

			return 1;
		}

		case 0x76:
		{
			// Memory card
			if (send_buffer[1] == 0x74) {
				// Read from card
				// Packet (port 1): e0 01 0a 76 74 00 00 02 00 00 00 01 f8
				// Packet (port 2): e0 01 0a 76 74 80 00 02 00 00 00 01 78
				m_current_device = DEVICE_MEMORY_CARD;

				m_memcard_port = send_buffer[2] >> 7;
				m_memcard_addr = (((send_buffer[2] << 8) | send_buffer[3]) & 0x7fff);

#ifdef K573MCR_DEBUG
				printf("jvs memcard read: %d %04x\n", m_memcard_port, m_memcard_addr);
				printf("\t");
				for (int i = 0; i < 10; i++) {
					printf("%02x ", send_buffer[i]);
				}
				printf("\n");
#endif

				if (m_memcard_status[m_memcard_port] != MEMCARD_UNAVAILABLE) {
					if (memcard_read(m_memcard_port, m_memcard_addr, nullptr)) {
						m_memcard_status[m_memcard_port] = MEMCARD_READING;
					} else {
						m_memcard_status[m_memcard_port] = MEMCARD_ERROR;
					}
				} else {
					m_memcard_status[m_memcard_port] = MEMCARD_UNAVAILABLE;
				}

#ifdef K573MCR_DEBUG
				printf("\nstatus: 1[%04x] 2[%04x]\n\n", m_memcard_status[0], m_memcard_status[1]);
#endif

				*recv_buffer++ = 0x01;
				*recv_buffer++ = 0x01;

				return 9;
			} else if (send_buffer[1] == 0x75) {
				// Write to card
				// Packet: e0 01 0a 76 75 02 00 00 00 3f 00 01 38
				// Packet: e0 01 0a 76 75 02 00 00 00 83 00 01 7c
				// Packet: e0 01 0a 76 75 02 00 00 00 88 00 01 81
				// Packet: e0 01 0a 76 75 02 00 00 00 b4 00 01 ad
				m_current_device = DEVICE_MEMORY_CARD;

				m_memcard_port = send_buffer[5] >> 7;
				m_memcard_addr = (((send_buffer[5] << 8) | send_buffer[6]) & 0x7fff);

				if (m_memcard_status[m_memcard_port] != MEMCARD_UNAVAILABLE) {
					if (memcard_write(m_memcard_port, m_memcard_addr, m_ram + m_ram_addr)) {
						m_memcard_status[m_memcard_port] = MEMCARD_WRITING;
					} else {
						m_memcard_status[m_memcard_port] = MEMCARD_ERROR;
					}
				} else {
					m_memcard_status[m_memcard_port] = MEMCARD_UNAVAILABLE;
				}

				*recv_buffer++ = 0x01;
				*recv_buffer++ = 0x01;

				return 9;
			}

#ifdef K573MCR_DEBUG
			printf("Unknown command!! 0x76 (mem card) %02x\n", send_buffer[1]);
			exit(1);
#endif

			return -1;
		}

		case 0x77:
		{
			// Controller ports
			// Packet: e0 01 02 77 7a
			//
			// This was used in Guitar Freaks starting with GF 2nd Mix Link Kit 2
			// which allowed players to bring their own PS1 compatible guitars
			// to the arcade.

			*recv_buffer++ = 0x01;
			pad_read(0, recv_buffer);
			pad_read(1, recv_buffer + 2);

#ifdef K573MCR_DEBUG
			printf("pad: %02x %02x %02x %02x\n", recv_buffer[0], recv_buffer[1], recv_buffer[2], recv_buffer[3]);
#endif

			recv_buffer += 4;

			return 1;
		}
	}

#ifdef K573MCR_DEBUG
	if (send_buffer[0] > 0x60 && send_buffer[0] < 0x80) {
		printf("Found unimplemented opcode: %02x\n", send_buffer[0]);
		exit(1);
	}
#endif

	// Command not recognized, pass it off to the base message handler
	return -1;
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
