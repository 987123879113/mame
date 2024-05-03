// license:BSD-3-Clause
// copyright-holders:smf, windyfairy
#include "emu.h"
#include "k573dio.h"

#define LOG_FPGA       (1U << 1)
#define LOG_MP3        (1U << 2)
// #define VERBOSE        (LOG_GENERAL | LOG_FPGA | LOG_MP3)
// #define LOG_OUTPUT_STREAM std::cout

#include "logmacro.h"

#define LOGFPGA(...)       LOGMASKED(LOG_FPGA, __VA_ARGS__)
#define LOGMP3(...)        LOGMASKED(LOG_MP3, __VA_ARGS__)

/*
  Digital I/O PCB
  ---------------

  GX894-PWB(B)A (C)1999 KONAMI CO. LTD.

             |-------------|
             |        CN12 |
             |             |
             | PC847 PC847 |
             |             |
             |        CN11 |
             |             |
             | PC847 PC847 |
             |             |
             | DS2401 CN10 |
             |             |
             | PC847 PC847 |
             |             |
             |  CN14  CN13 |
  |----------|             |----------|
  |                  PC847            |
  | ADM232 CN17              XC9536   |
  |                                   |
  |                    19.6608MHz     |-----------|
  | ADM232 CN15  CY7C109                          |
  |                       HY51V65164A HY51V65164A |
  |                            HY51V65164A        |
  |      CN16    XCS40XL                          |
  |                                               |
  | AK4309B   CN18         29.450MHz  MAS3507D    |
  |                                               |
  |                           CN3                 |
  | HYC2485S  RCA-1/2                             |
  |-----------------------------------------------|

  Notes:

  PC847       - High Density Mounting Type Photocoupler
  CN12        - 13 pin connector with 8 wires to external connectors
  CN11        - 12 pin connector with 8 wires to external connectors
  DS2401      - DS2401 911C2  Silicon serial number
  CN10        - 10 pin connector with 8 wires to external connectors
  CN14        - 7 pin connector
  CN13        - 5 pin connector with 2 wires to external connectors
  ADM232      - ADM232AARN 9933 H48475  High Speed, 5 V, 0.1 uF CMOS RS-232 Drivers/Receivers
  CN17        - 3 pin connector
  XC9536      - XILINX XC9536 PC44AEM9933 F1096429A 15C
  CN15        - 8 pin connector
  CY7C109     - CY7C109-25VC 931 H 04 404825  128k x 8 Static RAM
  HY51V65164A - 64M bit dynamic EDO RAM
  CN16        - 4 pin connector joining this PCB to the CD-DA IN on the MAIN PCB.
  XCS40XL     - XILINX XCS40XL PQ208AKP9929 A2033251A 4C
  AK4309B     - AKM AK4309B 3N932N  16bit SCF DAC
  CN18        - 6 pin connector
  MAS3507D    - IM MAS3507D D8 9173 51 HM U 072953.000 ES  MPEG 1/2 Layer 2/3 Audio Decoder
  CN3         - Connector joining this PCB to the MAIN PCB
  HYC2485S    - RS485 transceiver
  RCA-1/2     - RCA connectors for network communication

*/

constexpr int FPGA_RAM_MASK = 0x1fffffe;

enum {
	MPEG_ENABLE = 13,
	MPEG_STREAMING_ENABLE = 14,
	MPEG_FRAME_COUNTER_ENABLE = 15,
};

enum {
	PLAYBACK_STATE_DEMAND = 12,
	PLAYBACK_STATE_CRC_ERROR = 13,
	PLAYBACK_STATE_PLAYING = 14,
	PLAYBACK_STATE_ENABLED = 15,
};

DEFINE_DEVICE_TYPE(KONAMI_573_DIGITAL_IO_BOARD, k573dio_device, "k573_dio", "Konami 573 digital I/O board")

void k573dio_device::amap(address_map &map)
{
	map(0x00, 0xff).r(FUNC(k573dio_device::dummy_r<0xffff>)); // all registers default to 0xffff until FPGA is fully running the firmware

	map(0xf0, 0xff).r(FUNC(k573dio_device::fpga_status_r)); // the entire 0xf0 range returns the FPGA status
	map(0xf6, 0xf7).w(FUNC(k573dio_device::fpga_status_w));
	map(0xf8, 0xf9).w(FUNC(k573dio_device::fpga_firmware_w));

	// all registers below are only available after the FPGA firmware is loaded
	map(0x80, 0x8f).r(FUNC(k573dio_device::dummy_r<0x1234>));

	map(0x90, 0x9f).r(FUNC(k573dio_device::dummy_r<0x1234>));
	map(0x90, 0x91).w(FUNC(k573dio_device::network_id_w));
	// map(0x92, 0x93).w(FUNC(k573dio_device::network_unk_w)); // 0x11 gets written here

	map(0xa0, 0xa1).rw(FUNC(k573dio_device::mpeg_current_adr_high_r), FUNC(k573dio_device::mpeg_current_adr_high_w));
	map(0xa2, 0xa3).rw(FUNC(k573dio_device::mpeg_current_adr_low_r), FUNC(k573dio_device::mpeg_current_adr_low_w));
	map(0xa4, 0xa5).rw(FUNC(k573dio_device::mpeg_ctrl_r), FUNC(k573dio_device::mpeg_end_adr_high_w));
	map(0xa6, 0xa7).rw(FUNC(k573dio_device::mpeg_ctrl_r), FUNC(k573dio_device::mpeg_end_adr_low_w));
	map(0xa8, 0xa9).rw(FUNC(k573dio_device::mpeg_frame_counter_r), FUNC(k573dio_device::mpeg_key_1_w));
	map(0xaa, 0xab).rw(FUNC(k573dio_device::mpeg_status_r), FUNC(k573dio_device::mas_ctrl_w));
	map(0xac, 0xad).rw(FUNC(k573dio_device::mas_i2c_r), FUNC(k573dio_device::mas_i2c_w));
	map(0xae, 0xaf).rw(FUNC(k573dio_device::mpeg_ctrl_r), FUNC(k573dio_device::mpeg_ctrl_w)); // return value is mirrored by 0xa4 and 0xa6

	map(0xb0, 0xbf).r(FUNC(k573dio_device::ram_peek_r)); // all registers in the 0xb0 range will return the next value, but only 0xb4 will increment the address
	map(0xb0, 0xb1).w(FUNC(k573dio_device::ram_write_adr_high_w));
	map(0xb2, 0xb3).w(FUNC(k573dio_device::ram_write_adr_low_w));
	map(0xb4, 0xb5).rw(FUNC(k573dio_device::ram_r), FUNC(k573dio_device::ram_w));
	map(0xb6, 0xb7).w(FUNC(k573dio_device::ram_read_adr_high_w));
	map(0xb8, 0xb9).w(FUNC(k573dio_device::ram_read_adr_low_w));
	map(0xba, 0xbb).w(FUNC(k573dio_device::ram_ctrl_w));

	map(0xc0, 0xc1).rw(FUNC(k573dio_device::network_r), FUNC(k573dio_device::network_w));
	map(0xc2, 0xc3).r(FUNC(k573dio_device::network_output_buf_size_r));
	map(0xc4, 0xc5).r(FUNC(k573dio_device::network_input_buf_size_r));
	map(0xc6, 0xc7).r(FUNC(k573dio_device::dummy_r<0x7654>));
	map(0xc8, 0xc9).rw(FUNC(k573dio_device::dummy_r<0x7654>), FUNC(k573dio_device::network_reset_w));
	map(0xca, 0xcb).r(FUNC(k573dio_device::mpeg_timer_high_r));
	map(0xcc, 0xcd).rw(FUNC(k573dio_device::mpeg_timer_low_r), FUNC(k573dio_device::mpeg_timer_low_w));
	map(0xce, 0xcf).r(FUNC(k573dio_device::mpeg_timer_diff_r));

	map(0xd0, 0xdf).r(FUNC(k573dio_device::dummy_r<0x1234>));

	map(0xe0, 0xef).r(FUNC(k573dio_device::digital_id_r));
	map(0xe0, 0xe1).w(FUNC(k573dio_device::output_w<1>));
	map(0xe2, 0xe3).w(FUNC(k573dio_device::output_w<0>));
	map(0xe4, 0xe5).w(FUNC(k573dio_device::output_w<3>));
	map(0xe6, 0xe7).w(FUNC(k573dio_device::output_w<7>));
	map(0xe8, 0xe9).w(FUNC(k573dio_device::device_ctrl_w));
	map(0xea, 0xeb).w(FUNC(k573dio_device::mpeg_key_2_w));
	map(0xec, 0xed).w(FUNC(k573dio_device::mpeg_key_3_w));
	map(0xee, 0xef).w(FUNC(k573dio_device::digital_id_w));

	map(0xfa, 0xfb).w(FUNC(k573dio_device::output_w<4>));
	map(0xfc, 0xfd).w(FUNC(k573dio_device::output_w<5>));
	map(0xfe, 0xff).w(FUNC(k573dio_device::output_w<2>));
}

k573dio_device::k573dio_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, KONAMI_573_DIGITAL_IO_BOARD, tag, owner, clock),
	m_ram(*this, "ram", 0x1800000, ENDIANNESS_LITTLE),
	m_digital_id(*this, "digital_id"),
	m_mas3507d(*this, "mpeg"),
	m_network(*this, "dio_network%u", 0U),
	output_cb(*this),
	m_is_ddrsbm_fpga(false)
{
}

void k573dio_device::device_start()
{
	save_item(NAME(m_output_data));

	save_item(NAME(m_ram_addr));
	save_item(NAME(m_last_valid_ram_read));

	save_item(NAME(m_fpga_status));
	save_item(NAME(m_is_fpga_initialized));
	save_item(NAME(m_firmware_bits_received));
	save_item(NAME(m_firmware_byte));
	save_item(NAME(m_firmware));

	save_item(NAME(m_mpeg_sample_counter));
	save_item(NAME(m_mpeg_sample_counter_last));
	save_item(NAME(m_mpeg_current_addr));
	save_item(NAME(m_mpeg_end_addr));
	save_item(NAME(m_mpeg_ctrl));
	save_item(NAME(m_mpeg_status));
	save_item(NAME(m_mpeg_frame_counter));
	save_item(NAME(m_mpeg_current_has_ended));
	save_item(NAME(m_mpeg_timer_enabled));

	save_item(NAME(m_crypto_key1));
	save_item(NAME(m_crypto_key2));
	save_item(NAME(m_crypto_key3));

	save_item(NAME(m_mp3_remaining_bytes));
	save_item(NAME(m_mp3_data));

	save_item(NAME(m_digital_id_cached));

	save_item(NAME(m_network_id));
	save_item(NAME(m_network_output_buf_size));

	save_item(NAME(m_device_ctrl));
	save_item(NAME(m_ram_enabled));
	save_item(NAME(m_mas3507d_dac_output_enabled));
	save_item(NAME(m_external_audio_muted));

	save_item(NAME(m_ram_ctrl));
	save_item(NAME(m_fpga_pre_init));

	m_stream_timer = timer_alloc(FUNC(k573dio_device::mpeg_data_transfer), this);

	// Default memory state after FPGA initialization is alternating 0x1000 blocks of 0xFFFFs and 0x0000s
	for (int i = 0; i < m_ram.length() / 0x1000 / 2; i++)
		std::fill_n(&m_ram[i * 0x1000 / 2], 0x1000 / 2, (i & 1) ? 0x0000 : 0xffff);
}

void k573dio_device::device_reset()
{
	m_fpga_status = 0x8fff;

	// TODO: Does this reset when the FPGA is re-initialized?
	std::fill(std::begin(m_output_data), std::end(m_output_data), 0);

	reset_fpga_state();
}

void k573dio_device::reset_fpga_state()
{
	m_ram_addr = 0;
	m_last_valid_ram_read = 0; // RAM peek registers show 0 on initialization

	m_fpga_status &= ~(0x4000 | 0x2000);
	m_is_fpga_initialized = false;
	m_firmware_bits_received = 0;
	m_fpga_pre_init = false;
	m_firmware_byte = 0;
	std::fill(std::begin(m_firmware), std::end(m_firmware), 0);

	m_mpeg_sample_counter = m_mpeg_sample_counter_last = 0;
	m_mpeg_current_addr = m_mpeg_end_addr = 0;

	m_crypto_key1 = m_crypto_key2 = m_crypto_key3 = 0;

	m_network_id = 0;
	m_network_output_buf_size = 0;
	m_network_buffer_muxed.clear();
	m_network_buffer_output.clear();
	m_network_buffer_output_queue.clear();
	for (int i = 0; i < NETWORK_CONNECTIONS; i++)
		m_network_buffer_input[i].clear();

	m_mpeg_ctrl = 0;
	m_mpeg_status = 1 << PLAYBACK_STATE_CRC_ERROR;
	m_mpeg_frame_counter = 0;
	m_mpeg_current_has_ended = false;
	m_mpeg_timer_enabled = true;

	m_mp3_remaining_bytes = 0;
	m_mp3_data = 0;

	m_digital_id_cached = 0;

	m_device_ctrl = 0;
	m_ram_enabled = false;
	m_mas3507d_dac_output_enabled = false;

	m_external_audio_muted = false;

	m_ram_ctrl = 0;
}

ROM_START( k573dio )
	ROM_REGION( 0x000008, "digital_id", 0 )
	ROM_LOAD( "digital-id.bin",   0x000000, 0x000008, CRC(2b977f4d) SHA1(2b108a56653f91cb3351718c45dfcf979bc35ef1) )
ROM_END

const tiny_rom_entry *k573dio_device::device_rom_region() const
{
	return ROM_NAME(k573dio);
}

void k573dio_device::device_add_mconfig(machine_config &config)
{
	MAS3507D(config, m_mas3507d, 29'450'000);
	m_mas3507d->mpeg_frame_sync_cb().set(*this, FUNC(k573dio_device::mpeg_frame_sync));
	m_mas3507d->demand_cb().set(*this, FUNC(k573dio_device::mpeg_demand));
	m_mas3507d->mpeg_crc_error_cb().set(*this, FUNC(k573dio_device::mpeg_crc_error));
	m_mas3507d->i2s_soi_cb().set(*this, FUNC(k573dio_device::mpeg_frame_identification));
	m_mas3507d->add_route(0, ":lspeaker", 1.0);
	m_mas3507d->add_route(1, ":rspeaker", 1.0);

	DS2401(config, m_digital_id);

	for (int i = 0; i < m_network.size(); i++)
	{
		BITBANGER(config, m_network[i], 0);
	}

	TIMER(config, "network_timer").configure_periodic(FUNC(k573dio_device::network_update_callback), attotime::from_hz(300));
}

template <uint16_t Val>
uint16_t k573dio_device::dummy_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	return Val;
}

uint16_t k573dio_device::fpga_status_r()
{
	LOGFPGA("%s: fpga_status_r (%s)\n", tag(), machine().describe_context());
	return (m_fpga_status & ~0x4000) | 0x8fff; // 0x8000 is always set and 0x4000 is never set
}

void k573dio_device::fpga_status_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
	{
		// 0x2000 and 0x8000 set m_fpga_status | 0x1000 immediately but 0x1000 and 0x4000 only set m_fpga_status | 0x1000 after the second write
		if (((data & 0x2000) || (data & 0x8000)) && !m_fpga_pre_init)
		{
			m_fpga_status &= ~(0x2000 | 0x8000);
			m_fpga_status |= 0x1000;
			m_fpga_pre_init = true;
		}

		if ((data & 0x1000) || (data & 0x4000))
		{
			if (m_fpga_pre_init)
				m_fpga_status ^= 0x1000;
			else
				m_fpga_pre_init = true;
		}
	}
	else
	{
		if ((m_fpga_status & 0x4000) && !(data & 0x4000))
			reset_fpga_state();

		m_fpga_status = data;
	}
}

void k573dio_device::fpga_firmware_w(uint16_t data)
{
	if (m_is_fpga_initialized || !(m_fpga_status & 0x1000))
		return;

	m_firmware_byte |= BIT(data, 15) << (m_firmware_bits_received % 8);
	m_firmware_bits_received++;

	if (m_firmware_bits_received > 0 && (m_firmware_bits_received % 8) == 0)
	{
		const auto offs = (m_firmware_bits_received / 8) - 1;
		m_firmware[offs] = m_firmware_byte;
		m_firmware_byte = 0;
	}

	if (m_firmware_bits_received == FPGA_PROM_SIZE * 8)
	{
		// Assume the firmware that was sent is actually valid and just accept it
		m_fpga_status |= 0x2000 | 0x4000;
		m_is_fpga_initialized = true;

		// 5ff290d6 The standard FPGA program used by most games
		// f1622789 First found in Mambo A Go-Go, also found in the bootloader of most later games
		// c0d58bca Used only by DDR Solo Bass Mix, different encryption algorithm for MP3s
		const uint32_t firmware_crc = util::crc32_creator::simple(m_firmware, FPGA_PROM_SIZE);
		if (firmware_crc == 0xc0d58bca)
			m_is_ddrsbm_fpga = true;

		LOG("FPGA firmware crc: %08x (%d)\n", firmware_crc, m_is_ddrsbm_fpga);
	}
}

uint16_t k573dio_device::digital_id_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	// Another device is made available at bit 8 but it's unpopulated on the PCB

	if (!machine().side_effects_disabled())
		m_digital_id_cached = (m_digital_id->read() << 12) | (1 << 8);

	return m_digital_id_cached;
}

void k573dio_device::digital_id_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_digital_id->write(!BIT(data, 12));
}

uint16_t k573dio_device::mas_i2c_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	const int scl = m_mas3507d->i2c_scl_r();
	const int sda = m_mas3507d->i2c_sda_r();
	return (scl << 13) | (sda << 12);
}

void k573dio_device::mas_i2c_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_mas3507d->i2c_scl_w(BIT(data, 13));
	m_mas3507d->i2c_sda_w(BIT(data, 12));
}

void k573dio_device::ram_write_adr_high_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_ram_addr = ((data << 16) | (m_ram_addr & 0x0000ffff)) & FPGA_RAM_MASK;
}

void k573dio_device::ram_write_adr_low_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_ram_addr = ((m_ram_addr & 0xffff0000) | data) & FPGA_RAM_MASK;
}

void k573dio_device::ram_read_adr_high_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_ram_addr = ((data << 16) | (m_ram_addr & 0x0000ffff)) & FPGA_RAM_MASK;
	m_last_valid_ram_read = ram_get_next_value();
}

void k573dio_device::ram_read_adr_low_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_ram_addr = ((m_ram_addr & 0xffff0000) | data) & FPGA_RAM_MASK;
	m_last_valid_ram_read = ram_get_next_value(); // m_last_valid_ram_read is updated when changing the RAM read address, but not when changing the RAM write address
}

uint16_t k573dio_device::ram_get_next_value()
{
	const uint32_t addr = (m_ram_addr & FPGA_RAM_MASK) >> 1;

	if (addr < m_ram.length())
		return m_ram[addr];

	return m_last_valid_ram_read;
}

uint16_t k573dio_device::ram_peek_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	if (!m_ram_enabled)
		return m_last_valid_ram_read;

	return ram_get_next_value();
}

uint16_t k573dio_device::ram_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	if (!m_ram_enabled)
		return m_last_valid_ram_read;

	uint16_t r = m_last_valid_ram_read;
	const uint32_t addr = (m_ram_addr & FPGA_RAM_MASK) >> 1;

	if (addr < m_ram.length())
		r = m_ram[addr];

	if (!machine().side_effects_disabled())
	{
		m_last_valid_ram_read = r;

		m_ram_addr = (m_ram_addr + 2) & FPGA_RAM_MASK;
		m_last_valid_ram_read = ram_get_next_value();
	}

	return r;
}

void k573dio_device::ram_w(uint16_t data)
{
	if (!m_is_fpga_initialized || !m_ram_enabled)
		return;

	const uint32_t addr = (m_ram_addr & FPGA_RAM_MASK) >> 1;

	if (addr < m_ram.length())
		m_ram[addr] = data;

	m_ram_addr = (m_ram_addr + 2) & FPGA_RAM_MASK;
}

void k573dio_device::ram_ctrl_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_ram_ctrl = data;
}

void k573dio_device::mpeg_current_adr_high_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_mpeg_current_addr = ((data << 16) | (m_mpeg_current_addr & 0xffff)) & FPGA_RAM_MASK;

	LOGMP3("FPGA MPEG start address high %04x (%08x)\n", data, m_mpeg_current_addr);
}

void k573dio_device::mpeg_current_adr_low_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_mpeg_current_addr = ((m_mpeg_current_addr & 0xffff0000) | data) & FPGA_RAM_MASK;

	LOGMP3("FPGA MPEG start address low %04x (%08x)\n", data, m_mpeg_current_addr);
}

void k573dio_device::mpeg_end_adr_high_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_mpeg_end_addr = ((data << 16) | (m_mpeg_end_addr & 0xffff)) & FPGA_RAM_MASK;

	LOGMP3("FPGA MPEG end address high %04x (%08x)\n", data, m_mpeg_end_addr);
}

void k573dio_device::mpeg_end_adr_low_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_mpeg_end_addr = ((m_mpeg_end_addr & 0xffff0000) | data) & FPGA_RAM_MASK;

	LOGMP3("FPGA MPEG end address low %04x (%08x)\n", data, m_mpeg_end_addr);
}

uint16_t k573dio_device::mpeg_current_adr_high_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	return (m_mpeg_current_addr & FPGA_RAM_MASK) >> 16;
}

uint16_t k573dio_device::mpeg_current_adr_low_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	return m_mpeg_current_addr & FPGA_RAM_MASK;
}

void k573dio_device::mpeg_key_1_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	LOGMP3("FPGA MPEG key 1/3 %04x\n", data);
	m_crypto_key1 = data;

	if (m_is_ddrsbm_fpga)
		m_crypto_key3 = 0;
}

void k573dio_device::mpeg_key_2_w(uint16_t data)
{
	if (!m_is_fpga_initialized || m_is_ddrsbm_fpga)
		return;

	LOGMP3("FPGA MPEG key 2/3 %04x\n", data);
	m_crypto_key2 = data;
}

void k573dio_device::mpeg_key_3_w(uint16_t data)
{
	if (!m_is_fpga_initialized || m_is_ddrsbm_fpga)
		return;

	LOGMP3("FPGA MPEG key 3/3 %04x\n", data);
	m_crypto_key3 = data;
}

uint16_t k573dio_device::mpeg_frame_counter_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	return m_mpeg_frame_counter;
}

uint16_t k573dio_device::mpeg_status_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	// TODO: Figure out what enables/disables PLAYBACK_STATE_ENABLED
	return m_mpeg_status | (1 << PLAYBACK_STATE_ENABLED);
}

uint16_t k573dio_device::mpeg_ctrl_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	const int is_streaming = BIT(m_mpeg_ctrl, MPEG_STREAMING_ENABLE)
		&& (m_mpeg_current_addr != m_mpeg_end_addr || (m_mpeg_current_addr == m_mpeg_end_addr && m_mp3_remaining_bytes != 0))
		&& !m_mpeg_current_has_ended;

	return is_streaming << 12;
}

void k573dio_device::mas_ctrl_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	// Set MAS3507D /POR (Reset) = BIT(m_device_ctrl, 15) && BIT(data, 12)
	// Set MAS3507D /PCS (PIO Chip Select) = BIT(m_device_ctrl, 15) && BIT(data, 13)

	LOG("mas_ctrl_w %04x\n", data);
}

void k573dio_device::mpeg_ctrl_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	LOG("FPGA MPEG control %c%c%c | %04x\n",
				BIT(data, MPEG_FRAME_COUNTER_ENABLE) ? '#' : '.',
				BIT(data, MPEG_STREAMING_ENABLE) ? '#' : '.',
				BIT(data, MPEG_ENABLE) ? '#' : '.',
				data);

	if (BIT(m_mpeg_ctrl, MPEG_FRAME_COUNTER_ENABLE) && !BIT(data, MPEG_FRAME_COUNTER_ENABLE))
		m_mpeg_frame_counter = 0;

	// Both of these flags need to be enabled to start the timer again after it is cleared
	// Note: Does not reset the counter on ddrsbm firmware
	if (!m_is_ddrsbm_fpga && (BIT(data, MPEG_STREAMING_ENABLE) && BIT(data, MPEG_ENABLE)) && !(BIT(m_mpeg_ctrl, MPEG_STREAMING_ENABLE) && BIT(m_mpeg_ctrl, MPEG_ENABLE)))
		m_mpeg_sample_counter = m_mpeg_sample_counter_last = 0;

	if (m_mpeg_current_has_ended && ((!BIT(data, MPEG_STREAMING_ENABLE) && BIT(m_mpeg_ctrl, MPEG_STREAMING_ENABLE)) || (!BIT(data, MPEG_ENABLE) && BIT(m_mpeg_ctrl, MPEG_ENABLE))))
	{
		m_mpeg_current_has_ended = false;
		m_mas3507d->mpeg_state_reset();
	}

	m_mpeg_ctrl = data;
}

uint16_t k573dio_device::mpeg_timer_diff_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	// First read will show a larger value and and then clear out the bottom 16-bits of the timer register for further calculations
	const uint16_t diff = m_mpeg_sample_counter - m_mpeg_sample_counter_last;

	if (!machine().side_effects_disabled())
	{
		m_mpeg_sample_counter &= 0xffff0000;
		m_mpeg_sample_counter_last &= 0xffff0000;
	}

	return diff;
}

uint16_t k573dio_device::mpeg_timer_high_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	if (m_is_ddrsbm_fpga)
		return 0x7654; // not implemented in ddrsbm's firmware

	m_mpeg_sample_counter_last = m_mpeg_sample_counter;

	return m_mpeg_sample_counter >> 16;
}

uint16_t k573dio_device::mpeg_timer_low_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	m_mpeg_sample_counter_last = m_mpeg_sample_counter;

	return m_mpeg_sample_counter;
}

void k573dio_device::mpeg_timer_low_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	// data written here is ignored, any value written will reset the timer to 0
	m_mpeg_sample_counter = m_mpeg_sample_counter_last = 0;

	// ddrsbm's firmware doesn't stop the timer if you reset it when it's not streaming anything
	if (!m_is_ddrsbm_fpga)
		m_mpeg_timer_enabled = BIT(mpeg_ctrl_r(), 12);
}

TIMER_CALLBACK_MEMBER(k573dio_device::mpeg_data_transfer)
{
	if (!m_mpeg_current_has_ended
		&& BIT(m_mpeg_ctrl, MPEG_STREAMING_ENABLE)
		&& m_mpeg_current_addr == m_mpeg_end_addr
		&& m_mp3_remaining_bytes == 0)
	{
		m_mpeg_current_has_ended = true;
		m_mpeg_ctrl &= ~(1 << MPEG_STREAMING_ENABLE);
	}

	if (!BIT(m_mpeg_status, PLAYBACK_STATE_DEMAND)
		|| !BIT(m_mpeg_ctrl, MPEG_ENABLE)
		|| !BIT(m_mpeg_ctrl, MPEG_STREAMING_ENABLE)
		|| m_mpeg_current_has_ended)
	{
		return;
	}

	if (m_mp3_remaining_bytes == 0)
	{
		const int ram_enabled = BIT(m_ram_ctrl, 15);
		const uint16_t src = ram_enabled ? m_ram[m_mpeg_current_addr >> 1] : 0;
		m_mp3_data = m_is_ddrsbm_fpga ? decrypt_ddrsbm(src) : decrypt_default(src);
		m_mp3_data = ((m_mp3_data >> 8) & 0xff) | ((m_mp3_data & 0xff) << 8);
		m_mpeg_current_addr += 2;
		m_mp3_remaining_bytes = 2;
	}

	m_mas3507d->sid_w(m_mp3_data & 0xff);
	m_mp3_data >>= 8;
	m_mp3_remaining_bytes--;
}

void k573dio_device::mpeg_frame_sync(int state)
{
	m_mpeg_status &= ~(1 << PLAYBACK_STATE_PLAYING);
	m_mpeg_status |= state << PLAYBACK_STATE_PLAYING;

	if (state && BIT(m_mpeg_ctrl, MPEG_FRAME_COUNTER_ENABLE))
	{
		if (m_mpeg_frame_counter == 0)
			m_mpeg_timer_enabled = true;

		m_mpeg_frame_counter++;
	}
}

void k573dio_device::mpeg_demand(int state)
{
	const auto prev_status = m_mpeg_status;

	m_mpeg_status &= ~(1 << PLAYBACK_STATE_DEMAND);
	m_mpeg_status |= state << PLAYBACK_STATE_DEMAND;

	if (!BIT(prev_status, PLAYBACK_STATE_DEMAND) && BIT(m_mpeg_status, PLAYBACK_STATE_DEMAND))
		m_stream_timer->adjust(attotime::zero, 0, attotime::from_hz(44100));
	else if (BIT(prev_status, PLAYBACK_STATE_DEMAND) && !BIT(m_mpeg_status, PLAYBACK_STATE_DEMAND))
		m_stream_timer->adjust(attotime::never);
}

void k573dio_device::mpeg_crc_error(int state)
{
	// Set when the MP3 is corrupt or there's no MP3 data to be decoded
	m_mpeg_status &= ~(1 << PLAYBACK_STATE_CRC_ERROR);
	m_mpeg_status |= state << PLAYBACK_STATE_CRC_ERROR;
}

void k573dio_device::mpeg_frame_identification(int state)
{
	// Differentiates the L and R channel samples in the I2S stream
	// The FPGA uses this to count exactly how many samples have been played
	if (m_mas3507d_dac_output_enabled && m_mpeg_timer_enabled && state)
		m_mpeg_sample_counter++;
}

uint16_t k573dio_device::decrypt_default(uint16_t v)
{
	uint16_t m = m_crypto_key1 ^ m_crypto_key2;

	v = bitswap<16>(
		v,
		15 - BIT(m, 0xF),
		14 + BIT(m, 0xF),
		13 - BIT(m, 0xE),
		12 + BIT(m, 0xE),
		11 - BIT(m, 0xB),
		10 + BIT(m, 0xB),
		9 - BIT(m, 0x9),
		8 + BIT(m, 0x9),
		7 - BIT(m, 0x8),
		6 + BIT(m, 0x8),
		5 - BIT(m, 0x5),
		4 + BIT(m, 0x5),
		3 - BIT(m, 0x3),
		2 + BIT(m, 0x3),
		1 - BIT(m, 0x2),
		0 + BIT(m, 0x2)
	);

	v ^= (BIT(m, 0xD) << 14) ^
		(BIT(m, 0xC) << 12) ^
		(BIT(m, 0xA) << 10) ^
		(BIT(m, 0x7) << 8) ^
		(BIT(m, 0x6) << 6) ^
		(BIT(m, 0x4) << 4) ^
		(BIT(m, 0x1) << 2) ^
		(BIT(m, 0x0) << 0);

	v ^= bitswap<16>(
		(uint16_t)m_crypto_key3,
		7, 0, 6, 1,
		5, 2, 4, 3,
		3, 4, 2, 5,
		1, 6, 0, 7
	);

	m_crypto_key1 = (m_crypto_key1 & 0x8000) | ((m_crypto_key1 << 1) & 0x7FFE) | ((m_crypto_key1 >> 14) & 1);

	if(((m_crypto_key1 >> 15) ^ m_crypto_key1) & 1)
		m_crypto_key2 = (m_crypto_key2 << 1) | (m_crypto_key2 >> 15);

	m_crypto_key3++;

	return v;
}

uint16_t k573dio_device::decrypt_ddrsbm(uint16_t data)
{
	uint8_t key[16] = {0};
	uint16_t key_state = bitswap<16>(
		m_crypto_key1,
		13, 11, 9, 7,
		5, 3, 1, 15,
		14, 12, 10, 8,
		6, 4, 2, 0
	);

	for(int i = 0; i < 8; i++)
	{
		key[i * 2] = key_state & 0xff;
		key[i * 2 + 1] = (key_state >> 8) & 0xff;
		key_state = ((key_state & 0x8080) >> 7) | ((key_state & 0x7f7f) << 1);
	}

	uint16_t output_word = 0;
	for(int cur_bit = 0; cur_bit < 8; cur_bit++)
	{
		int even_bit_shift = cur_bit * 2;
		int odd_bit_shift = cur_bit * 2 + 1;
		bool is_even_bit_set = data & (1 << even_bit_shift);
		bool is_odd_bit_set = data & (1 << odd_bit_shift);
		bool is_key_bit_set = key[m_crypto_key3 & 15] & (1 << cur_bit);
		bool is_scramble_bit_set = key[(m_crypto_key3 - 1) & 15] & (1 << cur_bit);

		if(is_scramble_bit_set)
			std::swap(is_even_bit_set, is_odd_bit_set);

		if(is_even_bit_set ^ is_key_bit_set)
			output_word |= 1 << even_bit_shift;

		if(is_odd_bit_set)
			output_word |= 1 << odd_bit_shift;
	}

	m_crypto_key3++;

	return output_word;
}

template <int Offset>
void k573dio_device::output_w(uint16_t data)
{
	static const int shift[] = { 0, 2, 3, 1 };

	if (!m_is_fpga_initialized)
		return;

	data = (data >> 12) & 0x0f;

	for(int i = 0; i < 4; i++)
	{
		const int oldbit = BIT(m_output_data[Offset], shift[i]);
		const int newbit = BIT(data, shift[i]);

		if(oldbit != newbit)
			output_cb(4 * Offset + i, newbit, 0xff);
	}

	m_output_data[Offset] = data;
}

uint16_t k573dio_device::network_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	uint16_t val = 0;

	if (m_network_buffer_muxed.size() > 0) {
		val = m_network_buffer_muxed.front();
		m_network_buffer_muxed.pop_front();
	}

	return val;
}

void k573dio_device::network_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	if ((m_network_buffer_output.size() == 0 && data != 0xc0) || (m_network_buffer_output.size() > 0 && m_network_buffer_output.front() != 0xc0))
		return;

	m_network_buffer_output.push_back(data);

	if (data != 0xc0 || m_network_buffer_output.size() <= 1 || m_network_buffer_output.front() != 0xc0 || m_network_buffer_output.back() != 0xc0)
		return;

	if (m_network_buffer_output.size() == 2) {
		// c0 c0 would be an empty packet (corrupt packet?) so discard the first byte
		m_network_buffer_output.pop_front();
		return;
	}

	m_network_output_buf_size += m_network_buffer_output.size();
	m_network_buffer_output_queue.push_back(m_network_buffer_output);
	m_network_buffer_output.clear();
}

uint16_t k573dio_device::network_output_buf_size_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	// Number of bytes in the output buffer waiting to be sent
	// Highest value this returns on real hardware is 0x31f
	return m_network_output_buf_size;
}

uint16_t k573dio_device::network_input_buf_size_r()
{
	if (!m_is_fpga_initialized)
		return 0xffff;

	// Number of bytes in the input buffer waiting to be read
	// This appears to be a ring buffer on real hardware and if you read from an empty buffer it goes from 0x0000 to 0xfffe
	return m_network_buffer_muxed.size();
}

void k573dio_device::network_reset_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	// Any value written here will reset the network buffers
	// Maybe resets all network-related states?
	m_network_output_buf_size = 0;
	m_network_buffer_muxed.clear();
	m_network_buffer_output.clear();
	m_network_buffer_output_queue.clear();
	for (int i = 0; i < NETWORK_CONNECTIONS; i++)
		m_network_buffer_input[i].clear();
}

void k573dio_device::network_id_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	// The network ID configured in the operator menu
	m_network_id = data;
}

void k573dio_device::device_ctrl_w(uint16_t data)
{
	if (!m_is_fpga_initialized)
		return;

	m_ram_enabled = BIT(data, 14);

	// SOI counting disabled, CLKO disabled so I2S output to DAC stops working too
	const bool mute_updated = m_mas3507d_dac_output_enabled != BIT(data, 13);
	m_mas3507d_dac_output_enabled = BIT(data, 13);

	if (mute_updated)
		update_audio_mute();

	if (!BIT(data, 15) && BIT(m_device_ctrl, 15))
	{
		// Set MAS3507D /POR (Reset) = 0
		// Set MAS3507D /PCS (PIO Chip Select) = 0
	}

	m_device_ctrl = data;
}

void k573dio_device::set_audio_mute(bool muted)
{
	m_external_audio_muted = muted;
	update_audio_mute();
}

void k573dio_device::update_audio_mute()
{
	m_mas3507d->set_output_gain(ALL_OUTPUTS, !m_mas3507d_dac_output_enabled || m_external_audio_muted ? 0.0 : 1.0);
}

TIMER_DEVICE_CALLBACK_MEMBER(k573dio_device::network_update_callback)
{
	uint32_t len = 0;

	for (auto i = 0; i < m_network.size(); i++) {
		if (!m_network[i]->exists()) {
			continue;
		}

		do {
			uint8_t val = 0;
			len = m_network[i]->input(&val, 1);

			if (len == 0 || ((m_network_buffer_input[i].size() == 0 && val != 0xc0) || (m_network_buffer_input[i].size() > 0 && m_network_buffer_input[i].front() != 0xc0)))
				continue;

			// Found start of packet or continuation of an existing packet
			m_network_buffer_input[i].push_back(val);

			// If it's not potentially the end of the packet, just skip the logic to send
			if (val != 0xc0 || m_network_buffer_input[i].size() <= 1 || m_network_buffer_input[i].front() != 0xc0 || m_network_buffer_input[i].back() != 0xc0)
				continue;

			if (m_network_buffer_input[i].size() == 2) {
				// c0 c0 would be an empty packet (corrupt packet?) so discard the first byte
				m_network_buffer_input[i].pop_front();
				continue;
			}

			// Found end of packet, push all contents of temp buffer to main buffer
			auto target_machine = m_network_buffer_input[i][1];
			if (m_network_id != target_machine) {
				// Only accept packets from other machines
				m_network_buffer_muxed.insert(m_network_buffer_muxed.end(), m_network_buffer_input[i].begin(), m_network_buffer_input[i].end());
			}

			m_network_buffer_input[i].clear();
		} while (len > 0);
	}

	if (m_network_buffer_output_queue.size() > 0) {
		auto packet = m_network_buffer_output_queue.front();
		m_network_buffer_output_queue.pop_front();

		for (auto n : m_network) {
			if (!n->exists()) {
				continue;
			}

			for (auto c : packet) {
				n->output(c);
			}
		}

		m_network_output_buf_size -= packet.size();
	}
}