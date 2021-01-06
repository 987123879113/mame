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
	device_t(mconfig, KONAMI_573_MEMORY_CARD_READER, tag, owner, clock)
{
}

void k573mcr_device::device_start()
{
	sense = true;
	rx = tx = false;
	input_idx_r = input_idx_w = 0;
	output_idx_r = output_idx_w = 0;
	memset(input_buffer, 0, 0x1000);
	memset(output_buffer, 0, 0x1000);
}

READ_LINE_MEMBER( k573mcr_device::sense_r )
{
	return sense;
}

READ_LINE_MEMBER( k573mcr_device::rx_r )
{
	return output_idx_w > 0;
}

READ_LINE_MEMBER( k573mcr_device::tx_r )
{
	return rx;
}

bool k573mcr_device::is_valid_command()
{
	if (input_idx_w < 3) {
		return false;
	}

	int command_size = input_buffer[2] + 3;
	if (input_idx_w < command_size) {
		return false;
	}

	uint8_t checksum = 0;
	for (int i = 1; i < command_size - 1; i++) {
		checksum += input_buffer[i];
	}

	return checksum == input_buffer[command_size - 1];
}

void k573mcr_device::process_command()
{
	int command_size = input_buffer[2] + 3;
	uint8_t command = input_buffer[3];

	if (command == 0xf0) {
		printf("JVS_RESET\n");
	} else if (command == 0xf1) {
		printf("JVS_SET_DEV_ID\n");

		output_buffer[output_idx_w++] = 0xe0;
		output_buffer[output_idx_w++] = 0x00; // Node ID? Errors if this isn't 0
		output_buffer[output_idx_w++] = 0x04; // Response message size + checksum
		output_buffer[output_idx_w++] = 0x01; // Status 1

		output_buffer[output_idx_w++] = 0x02; // Message size + checksum
		output_buffer[output_idx_w++] = 0x00;

		output_buffer[output_idx_w++] = 0x08; // Checksum

		sense = false;
	} else if (command == 0x2F) {
		printf("JVS_RESEND\n");
	} else if (command == 0x10) {
		printf("JVS_SET_DEV_ID\n");
	} else if (command == 0x10) {
		printf("JVS_GET_DEV_ID\n");
	} else if (command == 0x11) {
		printf("JVS_GET_COMMAND_REV\n");
	} else if (command == 0x12) {
		printf("JVS_GET_VERSION\n");
	} else if (command == 0x13) {
		printf("JVS_GET_TRANS_VERSION\n");
	} else if (command == 0x14) {
		printf("JVS_GET_FUNCTION_INFO\n");
	} else if (command == 0x73) {
		printf("PCB_FW_WRITE_DONE\n");
	} else if (command == 0x77) {
		printf("PS_CONTROLLER_GET\n");
	} else if (command == 0x70) {
		printf("control_buf\n");
	} else if (command == 0x71) {
		printf("make_status\n");
	} else if (command == 0x72) {
		printf("sec_plate\n");
	} else if (command == 0x76) {
		printf("memory_card_function\n");
	} else {
		printf("unknown command: %02x\n", command);
	}

	command_size = (command_size + 1) & 0xfffffffe;

	memmove(input_buffer, input_buffer + command_size, 0x1000 - command_size);
	input_idx_w -= command_size;
	input_idx_r -= command_size;
}

void k573mcr_device::jvs_input_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	printf("jvs_input_w( %08x, %08x, %02x %02x )\n", offset, mem_mask, data & 0xff, data >> 8 );

	input_buffer[input_idx_w++] = data & 0xff;
	input_buffer[input_idx_w++] = data >> 8;

	if (is_valid_command()) {
		for (int i = 0; i < input_idx_w; i++) {
			printf("%02x ", input_buffer[i]);
		}

		printf("\n\t");
		process_command();
	}
}

uint16_t k573mcr_device::jvs_input_r(offs_t offset, uint16_t mem_mask)
{
	// TODO: Verify what should actually be returned here on real hardware
	uint16_t data = input_buffer[input_idx_r++];
	data |= input_buffer[input_idx_r++] << 8;

	//logerror("jvs_input_r( %08x, %08x ) %02x %02x\n", offset, mem_mask, data & 0xff, data >> 8 );
	return data;
}

void k573mcr_device::jvs_output_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	logerror("jvs_output_w( %08x, %08x, %02x %02x )\n", offset, mem_mask, data & 0xff, data >> 8 );
}

uint16_t k573mcr_device::jvs_output_r(offs_t offset, uint16_t mem_mask)
{
	if (output_idx_w <= 0) {
		return 0;
	}

	uint16_t data = output_buffer[0] | (output_buffer[1] << 8);
	output_idx_w -= 2;

	if (output_idx_w < 0) {
		output_idx_w = 0;
	}

	memmove(output_buffer, output_buffer + 2, 0x1000 - 2);

	printf("jvs_output_r %02x %02x | %08x %04x | %02x\n", data & 0xff, data >> 8, offset, mem_mask, output_idx_w);

	return data;
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
