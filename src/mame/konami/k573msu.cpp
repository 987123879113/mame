// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 Multi Session Unit
 *
 */

#include <iostream>

#include "emu.h"
#include "k573msu.h"
#include "bus/rs232/rs232.h"

#define LOG_GENERAL    (1 << 0)
#define LOG_INT_ATA    (1 << 1)
#define LOG_INT_SERIAL (1 << 2)
#define LOG_FPGA       (1 << 3)
#define LOG_DSP        (1 << 4)

//#define VERBOSE      (LOG_DSP)
#define LOG_OUTPUT_STREAM std::cout

#include "logmacro.h"

/*

  PCB Layout of External Multisession Box
  ---------------------------------------

  GXA25-PWB(A)(C)2000 KONAMI
  |--------------------------------------------------------------------------|
  |CN9  ADM232  LS273        PC16552          PC16552         XC9536(1)  CN13|
  |DSW(8)  LS245   LS273            18.432MHz                        DS2401  |
  |LEDX16   |-------|      |-------|       |-------|      |-------|          |
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
  |-------------------|  |----------|
  | TLP2630  LV14     |__| ADM232   |
  |CN2                           CN1|
  |A2430         AK5330             |
  |                          RCA L/R|
  |                          RCA L/R|
  |ZUS1R50505   6379A  __           |
  |                   |  |   LM358  |
  |-------------------|  |----------|



  Notes:
  CPU IRQs
    IRQ handler is called by calculating irq_handlers[(cause >> 8) & 0x3c](...)
    IRQ 0 = HDD interrupt
    IRQ 1 = CD-ROM interrupt
    IRQ 4 = Serial/RS232 interrupt
    IRQ 5 = DSP interrupt
    IRQ 13 = Timer interrupt
*/

k573msu_device::k573msu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, KONAMI_573_MULTI_SESSION_UNIT, tag, owner, clock),
	digital_id(*this, "digital_id"),
	m_maincpu(*this, "maincpu"),
	m_duart_com(*this, "duart_com_%d", 0L),
	m_ata_cdrom(*this, "ata_cdrom"),
	m_dsp(*this, "dsp_%d", 0L)
{
}

static void k573msu_ata_devices(device_slot_interface& device)
{
	device.option_add("cdrom", ATAPI_FIXED_CDROM);
}

void k573msu_device::device_add_mconfig(machine_config &config)
{
	TX3927(config, m_maincpu, 25_MHz_XTAL); // TODO: Fix clock
	m_maincpu->set_addrmap(AS_PROGRAM, &k573msu_device::amap);
	m_maincpu->in_brcond<0>().set([]() { return 1; }); // writeback complete
	m_maincpu->in_brcond<1>().set([]() { return 1; }); // writeback complete
	m_maincpu->in_brcond<2>().set([]() { return 1; }); // writeback complete
	m_maincpu->in_brcond<3>().set([]() { return 1; }); // writeback complete

	ATA_INTERFACE(config, m_ata_cdrom).options(k573msu_ata_devices, "cdrom", nullptr, true);
	m_ata_cdrom->irq_handler().set(FUNC(k573msu_device::ata_interrupt<1>));
	m_ata_cdrom->slot(0).set_fixed(true);

	M48T58(config, "m48t58y", 0);

	DS2401(config, digital_id);

	// Serial for the 4 subboards
	PC16552D(config, "duart_com_0", 0);
	auto& duart_chan0(NS16550(config, "duart_com_0:chan1", 18.432_MHz_XTAL));
	auto& rs232_port1(RS232_PORT(config, "msu_port1", default_rs232_devices, nullptr));
	rs232_port1.rxd_handler().set("duart_com_0:chan1", FUNC(ins8250_uart_device::rx_w));
	rs232_port1.dcd_handler().set("duart_com_0:chan1", FUNC(ins8250_uart_device::dcd_w));
	rs232_port1.dsr_handler().set("duart_com_0:chan1", FUNC(ins8250_uart_device::dsr_w));
	rs232_port1.ri_handler().set("duart_com_0:chan1", FUNC(ins8250_uart_device::ri_w));
	rs232_port1.cts_handler().set("duart_com_0:chan1", FUNC(ins8250_uart_device::cts_w));
	duart_chan0.out_tx_callback().set("msu_port1", FUNC(rs232_port_device::write_txd));
	duart_chan0.out_dtr_callback().set("msu_port1", FUNC(rs232_port_device::write_dtr));
	duart_chan0.out_rts_callback().set("msu_port1", FUNC(rs232_port_device::write_rts));
	duart_chan0.out_int_callback().set(FUNC(k573msu_device::serial_interrupt<0>));

	auto& duart_chan1(NS16550(config, "duart_com_0:chan0", 18.432_MHz_XTAL));
	auto& rs232_port2(RS232_PORT(config, "msu_port2", default_rs232_devices, nullptr));
	rs232_port2.rxd_handler().set("duart_com_0:chan0", FUNC(ins8250_uart_device::rx_w));
	rs232_port2.dcd_handler().set("duart_com_0:chan0", FUNC(ins8250_uart_device::dcd_w));
	rs232_port2.dsr_handler().set("duart_com_0:chan0", FUNC(ins8250_uart_device::dsr_w));
	rs232_port2.ri_handler().set("duart_com_0:chan0", FUNC(ins8250_uart_device::ri_w));
	rs232_port2.cts_handler().set("duart_com_0:chan0", FUNC(ins8250_uart_device::cts_w));
	duart_chan1.out_tx_callback().set("msu_port2", FUNC(rs232_port_device::write_txd));
	duart_chan1.out_dtr_callback().set("msu_port2", FUNC(rs232_port_device::write_dtr));
	duart_chan1.out_rts_callback().set("msu_port2", FUNC(rs232_port_device::write_rts));
	duart_chan1.out_int_callback().set(FUNC(k573msu_device::serial_interrupt<1>));

	PC16552D(config, "duart_com_1", 0);
	auto& duart_chan2(NS16550(config, "duart_com_1:chan1", 18.432_MHz_XTAL));
	auto& rs232_port3(RS232_PORT(config, "msu_port3", default_rs232_devices, nullptr));
	rs232_port3.rxd_handler().set("duart_com_1:chan1", FUNC(ins8250_uart_device::rx_w));
	rs232_port3.dcd_handler().set("duart_com_1:chan1", FUNC(ins8250_uart_device::dcd_w));
	rs232_port3.dsr_handler().set("duart_com_1:chan1", FUNC(ins8250_uart_device::dsr_w));
	rs232_port3.ri_handler().set("duart_com_1:chan1", FUNC(ins8250_uart_device::ri_w));
	rs232_port3.cts_handler().set("duart_com_1:chan1", FUNC(ins8250_uart_device::cts_w));
	duart_chan2.out_tx_callback().set("msu_port3", FUNC(rs232_port_device::write_txd));
	duart_chan2.out_dtr_callback().set("msu_port3", FUNC(rs232_port_device::write_dtr));
	duart_chan2.out_rts_callback().set("msu_port3", FUNC(rs232_port_device::write_rts));
	duart_chan2.out_int_callback().set(FUNC(k573msu_device::serial_interrupt<2>));

	auto& duart_chan3(NS16550(config, "duart_com_1:chan0", 18.432_MHz_XTAL));
	auto& rs232_port4(RS232_PORT(config, "msu_port4", default_rs232_devices, nullptr));
	rs232_port4.rxd_handler().set("duart_com_1:chan0", FUNC(ins8250_uart_device::rx_w));
	rs232_port4.dcd_handler().set("duart_com_1:chan0", FUNC(ins8250_uart_device::dcd_w));
	rs232_port4.dsr_handler().set("duart_com_1:chan0", FUNC(ins8250_uart_device::dsr_w));
	rs232_port4.ri_handler().set("duart_com_1:chan0", FUNC(ins8250_uart_device::ri_w));
	rs232_port4.cts_handler().set("duart_com_1:chan0", FUNC(ins8250_uart_device::cts_w));
	duart_chan3.out_tx_callback().set("msu_port4", FUNC(rs232_port_device::write_txd));
	duart_chan3.out_dtr_callback().set("msu_port4", FUNC(rs232_port_device::write_dtr));
	duart_chan3.out_rts_callback().set("msu_port4", FUNC(rs232_port_device::write_rts));
	duart_chan3.out_int_callback().set(FUNC(k573msu_device::serial_interrupt<3>));

	auto& dsp0(TC9446F(config, "dsp_0", 0));
	dsp0.mpeg_frame_sync_cb().set(*this, FUNC(k573msu_device::mpeg_frame_sync<0>));
	dsp0.demand_cb().set(*this, FUNC(k573msu_device::audio_demand<0>));
	dsp0.add_route(0, ":lspeaker", 0.65);
	dsp0.add_route(1, ":rspeaker", 0.65);

	auto& dsp1(TC9446F(config, "dsp_1", 0));
	dsp1.mpeg_frame_sync_cb().set(*this, FUNC(k573msu_device::mpeg_frame_sync<1>));
	dsp1.demand_cb().set(*this, FUNC(k573msu_device::audio_demand<1>));
	dsp1.add_route(0, ":lspeaker", 0.65);
	dsp1.add_route(1, ":rspeaker", 0.65);

	auto& dsp2(TC9446F(config, "dsp_2", 0));
	dsp2.mpeg_frame_sync_cb().set(*this, FUNC(k573msu_device::mpeg_frame_sync<2>));
	dsp2.demand_cb().set(*this, FUNC(k573msu_device::audio_demand<2>));
	dsp2.add_route(0, ":lspeaker", 0.65);
	dsp2.add_route(1, ":rspeaker", 0.65);

	auto& dsp3(TC9446F(config, "dsp_3", 0));
	dsp3.mpeg_frame_sync_cb().set(*this, FUNC(k573msu_device::mpeg_frame_sync<3>));
	dsp3.demand_cb().set(*this, FUNC(k573msu_device::audio_demand<3>));
	dsp3.add_route(0, ":lspeaker", 0.65);
	dsp3.add_route(1, ":rspeaker", 0.65);

	TIMER(config, "fifo_timer").configure_periodic(FUNC(k573msu_device::fifo_timer_callback), attotime::from_hz(1000));
}


template<unsigned N>
void k573msu_device::mpeg_frame_sync(int state)
{
	if (!state)
		return;

	if (m_playback_frame[N] == 0)
		m_playback_timers[N] = m_playback_timers_base[N] = machine().time();

	m_playback_frame[N]++;
}

template<unsigned N>
void k573msu_device::audio_demand(int state)
{
	if (state && m_dsp_fifo_buffer[N].size() > 0) {
		auto c = m_dsp_fifo_buffer[N].front();
		m_dsp_fifo_buffer[N].pop_front();
		m_dsp[N]->audio_w(c);
	}
}

void k573msu_device::device_reset()
{
	std::fill(std::begin(m_dsp_fifo_read_len), std::end(m_dsp_fifo_read_len), 0);
	std::fill(std::begin(m_dsp_fifo_write_len), std::end(m_dsp_fifo_write_len), 0);
	std::fill(std::begin(m_dsp_unk_flags), std::end(m_dsp_unk_flags), 0);
	std::fill(std::begin(m_dsp_fifo_enabled), std::end(m_dsp_fifo_enabled), false);

	std::fill(std::begin(m_playback_frame), std::end(m_playback_frame), 0);
	std::fill(std::begin(m_playback_timers), std::end(m_playback_timers), attotime::zero);
	std::fill(std::begin(m_playback_timers_base), std::end(m_playback_timers_base), attotime::zero);

	m_dsp_fifo_status = 0;
	m_dsp_dest_flag = 0xffff;
	m_dsp_fifo_irq_triggered = false;
}

void k573msu_device::device_start()
{
}

template<unsigned N>
void k573msu_device::ata_interrupt(int state)
{
	LOGMASKED(LOG_INT_ATA, "ata_interrupt<%d> %d\n", N, state);
	m_maincpu->trigger_irq(N, state);
}

template<unsigned N>
void k573msu_device::serial_interrupt(int state)
{
	LOGMASKED(LOG_INT_SERIAL, "serial_interrupt<%d> %d\n", N, state);
	m_maincpu->trigger_irq(4, state);
}

TIMER_DEVICE_CALLBACK_MEMBER(k573msu_device::fifo_timer_callback)
{
	// TODO: Figure out exactly how to trigger this
	if (!m_dsp_fifo_irq_triggered) {
		m_maincpu->trigger_irq(5, ASSERT_LINE);
		m_dsp_fifo_irq_triggered = true;
	}
	else if (m_dsp_fifo_irq_triggered) {
		m_maincpu->trigger_irq(5, CLEAR_LINE);
		m_dsp_fifo_irq_triggered = false;
	}
}

void k573msu_device::update_playback_timer(int idx)
{
	auto cur = machine().time();

	m_playback_timers[idx] = cur;

	if (m_playback_frame[idx] == 0 || !BIT(m_dsp_unk_flags[0x28 + (idx / 2)], 7 + (8 * (1 - (idx & 1))))) {
		// This bit always seems to be non-zero when the specified DSP should be in playback mode
		m_playback_timers_base[idx] = cur;
	}
}

int cur_offset = 0;
uint16_t k573msu_device::fpga_dsp_read(offs_t offset, uint16_t mem_mask)
{
	auto r = 0;

	switch (offset * 2) {
	// For the DSP chips
	case 0x08: case 0x0a:
		r = ~m_dsp_unk_flags[offset];
		break;

	case 0x0c: case 0x0e:
		// Number of bytes to read into the FIFO
		r |= (m_dsp_fifo_enabled[offset - 6] ? std::min(std::max(m_dsp_fifo_len - (int)m_dsp_fifo_buffer[offset - 6].size(), 0), 0xff) : 0) << 8;
		r |= m_dsp_fifo_enabled[offset - 6 + 1] ? std::min(std::max(m_dsp_fifo_len - (int)m_dsp_fifo_buffer[offset - 6 + 1].size(), 0), 0xff) : 0;
		break;

	case 0x20:
		// Is read at all of the places that a MIACK should normally be used during DSP communication.
		// The code checks for if this is 0, 1, or non-0 so seems maybe a combined MIACK?
		for (int i = 0; i < 4; i++) {
			if (BIT(~m_dsp_dest_flag, 3 - i)) {
				for (int j = 0; j < 24; j++) {
					r |= m_dsp[i]->miack_r();
				}
			}
		}
		break;

	case 0x24: case 0x26:
		// Response data (24 bit word) from DSP
		// TODO: Find a place this is actually used and verify it
		for (int i = 0; i < 4; i++) {
			if (BIT(~m_dsp_dest_flag, 3 - i)) {
				for (int j = 0; j < 24; j++) {
					r = (r << 1) | m_dsp[i]->midio_r();
				}
				break;
			}
		}
		break;

	case 0x60:
		r = ((m_dsp_fifo_enabled[3] && m_dsp_fifo_buffer[3].size() >= m_dsp_fifo_len) << 3)
			| ((m_dsp_fifo_enabled[2] && m_dsp_fifo_buffer[2].size() >= m_dsp_fifo_len) << 2)
			| ((m_dsp_fifo_enabled[1] && m_dsp_fifo_buffer[1].size() >= m_dsp_fifo_len) << 1)
			| ((m_dsp_fifo_enabled[0] && m_dsp_fifo_buffer[0].size() >= m_dsp_fifo_len) << 0);
		break;

	case 0x4c: case 0x4e:
		// ??
		r = m_dsp_unk_flags[offset];
		break;

	case 0x50: case 0x52:
	case 0x54: case 0x56:
	case 0x58: case 0x5a:
	case 0x5c: case 0x5e:
	{
		// Playback timer
		auto idx = (offset - 0x28) / 2;

		if (!BIT(offset, 0)) {
			update_playback_timer(idx);
		}

		auto t = uint32_t((m_playback_timers[idx] - m_playback_timers_base[idx]).as_double() * 44100.0);

		if (offset & 1) {
			r = t & 0xffff;
		}
		else {
			r = (t >> 16) & 0xffff;
		}
		break;
	}

	// For the FPGA itself
	case 0xe00:
		// Setting this to 2 makes the code go down a path that seems to expect a device to exist on the serial port.
		r = 2;
		break;

	case 0xf00:
		// Xilinx FPGA version?
		// 5963 = XC9536?
		r = 0x5963;
		break;

	default:
		r = m_dsp_unk_flags[offset];
		break;
	}

	r &= 0xffff;

	if (offset != 0x10 && offset != 0x780 && !(offset >= 0x30 && offset <= 0x34) && offset != 6 && offset != 7) {
		LOGMASKED(LOG_DSP, "%s: fpga_dsp_read %08x | %04x\n", machine().describe_context().c_str(), offset * 2, r);
	}

	return r;
}

void k573msu_device::fpga_dsp_write(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (!(offset >= 0x30 && offset <= 0x34) && offset >= 4) {
		LOGMASKED(LOG_DSP, "%s: fpga_dsp_write %08x %08x\n", machine().describe_context().c_str(), offset * 2, data);
	}

	switch (offset * 2) {
	case 0x00: case 0x02:
	case 0x04: case 0x06:
		// MP3 data gets written here. Presumably the FPGA will be forwarding it to the DSP chips as required
		if (m_dsp_fifo_enabled[offset]) {
			m_dsp_fifo_buffer[offset].push_back(data >> 8);
			m_dsp_fifo_buffer[offset].push_back(data & 0xff);
		}
		break;

	case 0x08: case 0x0a:
		// Encodes 4 different 2 bit status flags, except in reverse order?
		// DSP 1 = bits 6-8
		// DSP 2 = bits 4-6
		// DSP 3 = bits 2-4
		// DSP 4 = bits 0-2
		m_dsp_unk_flags[offset] = data;
		break;

	case 0x0c: case 0x0e:
		m_dsp_fifo_read_len[(offset - 6) * 2] = BIT(data, 8, 8);
		m_dsp_fifo_read_len[(offset - 6) * 2 + 1] = BIT(data, 0, 8);
		break;

	case 0x20:
		// Encodes 4 different 1 bit status flags, except in reverse order?
		// DSP 1 = bits 3
		// DSP 2 = bits 2
		// DSP 3 = bits 1
		// DSP 4 = bits 0
		m_dsp_dest_flag = data;
		break;

	case 0x22:
		for (int i = 0; i < 4; i++) {
			m_dsp[i]->mics_w(data);
		}
		break;

	case 0x24:
		// upper 8 bits of 24-bit data
		// If 0x100 is set then it's invalid data??
		for (int i = 0; i < 8; i++) {
			for (int idx = 0; idx < 4; idx++) {
				if (BIT(~m_dsp_dest_flag, 3 - idx)) {
					m_dsp[idx]->midio_w(BIT(data, 7 - i));
				}
			}
		}
		break;

	case 0x26:
		// bottom 16 bits of 24-bit data
		for (int i = 0; i < 16; i++) {
			for (int idx = 0; idx < 4; idx++) {
				if (BIT(~m_dsp_dest_flag, 3 - idx)) {
					m_dsp[idx]->midio_w(BIT(data, 15 - i));
				}
			}
		}
		break;

	case 0x28:
		// Flag to reset the DSPs via the FPGA? Is only 0 or 1.
		// Only gets used when during the start sequence when the DSPs are expected to be reset.
		break;

	case 0x2a:
		// Unused?
		// More reversed 1 bit status fields?
		break;

	case 0x40:
		// More status flags:
		// idx = ((3 - dsp_idx) * 2) + x where x is 7, 8, 9
		// value = (val & 1) << idx
		break;

	case 0x58: case 0x5a:
	case 0x5c: case 0x5e:
		m_dsp_unk_flags[offset] = data;
		break;

	case 0x4c: case 0x4e:
	case 0x54: case 0x56:
	{
		// Encodes 5 status flags each.
		// value = (param_2 & 3) << 8 | (param_3 & 3) << 6 | (param_4 & 3) << 4 | (param_5 & 3) << 2 | param_6 & 3
		int idx = offset - 0x26;

		if (offset >= 0x2a)
			idx = offset - 0x28;

		// If bit 8 is set then playback is enabled
		// This is just a hack but reset buffers when the playback is stopped
		if (!BIT(data, 8)) {
			m_dsp_fifo_buffer[idx].clear();
			m_dsp[idx]->reset_playback();
		}

		m_dsp_fifo_enabled[idx] = BIT(data, 8);
		m_dsp_unk_flags[offset] = data;

		break;
	}

	case 0x50: case 0x52:
		// Encoding:
		// Shift is calcaulated as: (dsp_idx * 8) + bitoffset
		// (DAT_8003c91e & ~(3 << shift)) | ((val & 3) << shift)
		// Each individual DSP's status bits fit into 8 bits, so the bottom byte of offset 0x50 is for DSP 1 and top byte is for DSP 2
		// Each byte encodes 4 different 2-bit statuses
		m_dsp_unk_flags[offset] = data;
		break;

	case 0x60:
	case 0x62:
	case 0x64:
	case 0x66:
	case 0x68:
	{
		// If bit is set to 1 then the device has data to transfer?
		// For 0x60-0x66 it will read in data from the DSP if set to 1.
		// 0x68 calls thread 0x0c if set to 1.
		// My guess is that 0x60-0x68 all address specific things, but you read the status back through 0x60 for all.

		// Thread 0x0c seems to be "dspctrl".
		// If something has 0x10 set, then it writes to the DSP's serial, otherwise it reads.
		auto bit = offset - 0x30;
		if (data)
			m_dsp_fifo_status |= 1 << bit;
		else
			m_dsp_fifo_status &= ~(1 << bit);
		break;
	}
	}

	m_dsp_unk_flags[offset] = data;
}

uint16_t k573msu_device::fpga_read(offs_t offset, uint16_t mem_mask)
{
	if (offset == 1)
		return 1;

	LOGMASKED(LOG_FPGA, "%s: fpga_read %08x %08x\n", machine().describe_context().c_str(), offset * 2, mem_mask);

	return 0;
}

void k573msu_device::fpga_write(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (offset != 4)
		LOGMASKED(LOG_FPGA, "%s: fpga_write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 2, data, mem_mask);
}

void k573msu_device::amap(address_map& map)
{
	map(0x00000000, 0x0fffffff).ram().mirror(0x80000000).mirror(0xa0000000);

	//map(0x10000000, 0x1000000f).rw(m_ata_hdd, FUNC(ata_interface_device::cs0_r), FUNC(ata_interface_device::cs0_w)); // HDD - unused on real hardware
	//map(0x10000080, 0x1000008f).rw(m_ata_hdd, FUNC(ata_interface_device::cs1_r), FUNC(ata_interface_device::cs1_w));
	map(0x10100000, 0x1010000f).rw(m_ata_cdrom, FUNC(ata_interface_device::cs0_r), FUNC(ata_interface_device::cs0_w)); // CD
	map(0x10100080, 0x1010008f).rw(m_ata_cdrom, FUNC(ata_interface_device::cs1_r), FUNC(ata_interface_device::cs1_w));
	map(0x10200000, 0x1020000f).rw(FUNC(k573msu_device::fpga_read), FUNC(k573msu_device::fpga_write));
	map(0x10240004, 0x10240007).portr("IN1").nopw(); // write = LEDx16 near dipsw?
	map(0x10300000, 0x1030001f).rw(m_duart_com[0], FUNC(pc16552_device::read), FUNC(pc16552_device::write)).umask16(0xff);
	map(0x10320000, 0x1032001f).rw(m_duart_com[1], FUNC(pc16552_device::read), FUNC(pc16552_device::write)).umask16(0xff);
	map(0x10400000, 0x10400fff).rw(FUNC(k573msu_device::fpga_dsp_read), FUNC(k573msu_device::fpga_dsp_write));

	map(0x1fc00000, 0x1fc7ffff).rom().region("tmpr3927", 0);
}

static INPUT_PORTS_START( k573msu )
	PORT_START( "IN1" )
	PORT_BIT(0xff00ffff, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_DIPUNKNOWN_DIPLOC( 0x00010000, 0x00010000, "SW:1" )
	PORT_DIPUNKNOWN_DIPLOC( 0x00020000, 0x00020000, "SW:2" )
	PORT_DIPUNKNOWN_DIPLOC( 0x00040000, 0x00040000, "SW:3" )
	PORT_DIPUNKNOWN_DIPLOC( 0x00080000, 0x00080000, "SW:4" )
	PORT_DIPNAME( 0x00100000, 0x00100000, "Start Up Device" ) PORT_DIPLOCATION( "DIP SW:5" )
	PORT_DIPSETTING(          0x00100000, "CD-ROM Drive" )
	PORT_DIPSETTING(          0x00000000, "Hard Drive" )
	PORT_DIPUNKNOWN_DIPLOC( 0x00200000, 0x00200000, "SW:6" )
	PORT_DIPUNKNOWN_DIPLOC( 0x00400000, 0x00400000, "SW:7" )
	PORT_DIPUNKNOWN_DIPLOC( 0x00800000, 0x00800000, "SW:8" )
INPUT_PORTS_END

ROM_START( k573msu )
	ROM_REGION32_BE( 0x080000, "tmpr3927", 0 )
	ROM_LOAD16_WORD_SWAP( "flash.20t", 0x000000, 0x080000, CRC(b70c65b0) SHA1(d3b2bf9d3f8b1caf70755a0d7fa50ef8bbd758b8) ) // from "GXA25-PWB(A)(C)2000 KONAMI"

	ROM_REGION( 0x002000, "m48t58y", 0 )
	ROM_LOAD( "m48t58y.6t",   0x000000, 0x002000, CRC(609ef020) SHA1(71b87c8b25b9613b4d4511c53d0a3a3aacf1499d) )

	ROM_REGION( 0x000008, "digital_id", 0 )
	ROM_LOAD( "digital-id.bin",   0x000000, 0x000008, CRC(2b977f4d) SHA1(2b108a56653f91cb3351718c45dfcf979bc35ef1) )
ROM_END

const tiny_rom_entry* k573msu_device::device_rom_region() const
{
	return ROM_NAME(k573msu);
}

ioport_constructor k573msu_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(k573msu);
}

DEFINE_DEVICE_TYPE(KONAMI_573_MULTI_SESSION_UNIT, k573msu_device, "k573msu", "Konami 573 Multi Session Unit")
