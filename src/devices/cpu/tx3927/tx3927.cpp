// license:BSD-3-Clause
// copyright-holders:windyfairy

/*
 * Toshiba TX3927 emulation.
 * Based on MIPS I with extensions and peripherals.
 *
 * TODO:
 *  - Move opcode handler changes from mips1.cpp to this file
 *  - Properly implement the code to replace the memory_translate hack in mips1.cpp for this CPU
 *  - CCFG
 *  - SDRAM
 *  - ROM
 *  - DMA
 *  - PCI
 *  - PIO
 */

#include "emu.h"
#include "cpu/tx3927/tx3927.h"

#define LOG_TX39_TMR (1U << 4)
#define LOG_TX39_SIO (1U << 5)
#define LOG_TX39_IRC (1U << 6)
#define LOG_TX39_CCFG (1U << 7)
#define LOG_TX39_SDRAM (1U << 8)
#define LOG_TX39_ROM (1U << 9)
#define LOG_TX39_DMA (1U << 10)
#define LOG_TX39_PCI (1U << 11)
#define LOG_TX39_PIO (1U << 12)

#include <iostream>

//#define VERBOSE (LOG_TX39_SIO)
//#define VERBOSE (LOG_TX39_TMR|LOG_TX39_SIO|LOG_TX39_IRC|LOG_TX39_CCFG|LOG_TX39_SDRAM|LOG_TX39_ROM|LOG_TX39_DMA|LOG_TX39_PCI|LOG_TX39_PIO)
#define LOG_OUTPUT_STREAM std::cout

#include "logmacro.h"

DEFINE_DEVICE_TYPE(TX3927,      tx3927_device,    "tx3927",  "Toshiba TX3927")

tx3927_device::tx3927_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock, size_t icache_size, size_t dcache_size) :
	mips1_device_base(mconfig, TX3927, tag, owner, clock, 0x3927, icache_size, dcache_size, false),
	m_program_config("program", ENDIANNESS_BIG, 32, 32, 0, address_map_constructor(FUNC(tx3927_device::amap), this)),
	m_sio(*this, "sio%d", 0L)
{
}

void tx3927_device::device_add_mconfig(machine_config& config)
{
	mips1core_device_base::device_add_mconfig(config);

	TX3927_SIO(config, "sio0", 0);
	TX3927_SIO(config, "sio1", 0);
}

void tx3927_device::device_reset()
{
	mips1core_device_base::device_reset();

	m_irc_irscr = 0;
	m_irc_irssr = 0;
	m_irc_ircsr = (1 << IRCSR_IF) | 0x1f;
	m_irc_ircer = 0;
	m_irc_irimr = 0;
	std::fill(std::begin(m_irc_irilr_full), std::end(m_irc_irilr_full), 0);
	std::fill(std::begin(m_irc_irilr), std::end(m_irc_irilr), 0);
	std::fill(std::begin(m_irc_ircr), std::end(m_irc_ircr), 0);

	std::fill(std::begin(m_pio_flags), std::end(m_pio_flags), 0);

	for (int i = 0; i < 3; i++) {
		m_tmr[i].TMTCR = 0;
		m_tmr[i].TMTISR = 0;
		m_tmr[i].TMCPRA = 0xffffff;
		m_tmr[i].TMCPRB = 0xffffff;
		m_tmr[i].TMITMR = 0;
		m_tmr[i].TMCCDR = 0;
		m_tmr[i].TMPGMR = 0;
		m_tmr[i].TMWTMR = 0;
		m_tmr[i].TMTRR = 0;
	}

	m_ccfg = 0x0d;
	m_crir = 0x39270011;
	m_pcfg = 0;
	m_tear = 0;
	m_pdcr = 0;

	m_pci_istat = 0;
	m_pci_pcistat = 0x210;
	m_pcicmd = 0;
	m_pci_iba = 0;
	m_pci_mba = 0;
	m_pci_svid = 0;
	m_pci_ssvid = 0;
	m_pci_ml = 0xff;
	m_pci_mg = 0xff;
	m_pci_ip = 0x01;
	m_pci_il = 0x00;
	m_ipcidata = 0;
	m_pci_icmd = 0;
	m_pci_ibe = 0;
	m_pci_lbc = 0;
	m_pci_mmas = 0;
	m_pci_iomas = 0;
	m_pci_ipciaddr = 0;
	m_pci_ipcidata = 0;

	std::fill(std::begin(m_rom_rccr), std::end(m_rom_rccr), 0x1fc30000);
	m_rom_rccr[0] = 0x1fc3e280; // Should have BAI, B16, BBC, BME set based on input pins
	update_rom_config(0);
}

device_memory_interface::space_config_vector tx3927_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_PROGRAM, &m_program_config)
	};
}

void tx3927_device::amap(address_map& map)
{
	map(0xfffe8000, 0xfffe8fff).rw(FUNC(tx3927_device::sdram_read), FUNC(tx3927_device::sdram_write));
	map(0xfffe9000, 0xfffe9fff).rw(FUNC(tx3927_device::rom_read), FUNC(tx3927_device::rom_write));
	map(0xfffeb000, 0xfffebfff).rw(FUNC(tx3927_device::dma_read), FUNC(tx3927_device::dma_write));
	map(0xfffec000, 0xfffecfff).rw(FUNC(tx3927_device::irc_read), FUNC(tx3927_device::irc_write));
	map(0xfffed000, 0xfffedfff).rw(FUNC(tx3927_device::pci_read), FUNC(tx3927_device::pci_write));
	map(0xfffee000, 0xfffeefff).rw(FUNC(tx3927_device::ccfg_read), FUNC(tx3927_device::ccfg_write));
	map(0xfffef000, 0xfffef2ff).rw(FUNC(tx3927_device::tmr_read), FUNC(tx3927_device::tmr_write));
	map(0xfffef300, 0xfffef3ff).rw(m_sio[0], FUNC(tx3927_sio::read), FUNC(tx3927_sio::write));
	map(0xfffef400, 0xfffef4ff).rw(m_sio[1], FUNC(tx3927_sio::read), FUNC(tx3927_sio::write));
	map(0xfffef500, 0xfffef5ff).rw(FUNC(tx3927_device::pio_read), FUNC(tx3927_device::pio_write));
}

void tx3927_device::device_start()
{
	mips1core_device_base::device_start();

	m_program = &space(AS_PROGRAM);

	m_timer[0] = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(tx3927_device::update_timer<0>), this));
	m_timer[1] = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(tx3927_device::update_timer<1>), this));
	m_timer[2] = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(tx3927_device::update_timer<2>), this));

	update_timer_speed();
}

constexpr double TX3927_TIMER_DIVISOR = 32;
void tx3927_device::update_timer_speed()
{
	// TODO: Add support for counter clock select

	for (int i = 0; i < 3; i++) {
		auto clock_speed = 133000000.0 / 4; // IMCLK speed is clock speed (133 MHz) / 4

		auto divisor = 0;
		if (BIT(m_tmr[i].TMTCR, TMTCR_CCDE)) {
			// Counter clock divide enable
			divisor = m_tmr[i].TMCCDR;
		}
		clock_speed /= std::pow(2, divisor + 1);

		// Divide clock speed further for performance reasons
		// The timer will tick up an equivalent amount to make up for the speed difference at the current divisor
		clock_speed /= TX3927_TIMER_DIVISOR;

		auto imclk = attotime::from_hz(clock_speed);
		m_timer[i]->adjust(attotime::zero, 0, imclk);
	}
}

void tx3927_device::trigger_irq(int irq, int state)
{
	// IRQ vector priority, highest to lowest
	//  0 INT[0]
	//  1 INT[1]
	//  2 INT[2]
	//  3 INT[3]
	//  4 INT[4]
	//  5 INT[5]
	//  6 SIO[0]
	//  7 SIO[1]
	//  8 DMA
	//  9 PIO
	// 10 PCI
	// 11 (Reserved)
	// 12 (Reserved)
	// 13 TMR[0]
	// 14 TMR[1]
	// 15 TMR[2]

	if (state) {
		m_irc_irssr |= 1 << irq;
	}
	else {
		m_irc_irssr &= ~(1 << irq);
		m_irc_ircsr = (1 << IRCSR_IF) | 0x1f;
	}

	if (!BIT(m_irc_ircer, 0)) // Interrupts disabled
		return;

	m_cop0[COP0_Cause] &= ~CAUSE_IP;
	if (state) {
		// Find highest priority interrupt
		for (int i = 0; i < 16; i++) {
			int curlevel = 0;
			int curmask = BIT(m_irc_irimr, 0, 3);
			int curirq = BIT(m_irc_ircsr, IRCSR_IVL, 5);

			if (!BIT(m_irc_irssr, i))
				continue;

			if (m_irc_irilr[i] == 0) // Disabled IRQ
				continue;

			if (m_irc_irilr[i] < curmask) // Masked IRQ
				continue;

			if (!BIT(m_irc_ircsr, IRCSR_IF))
				curlevel = BIT(m_irc_ircsr, IRCSR_ILV, 3);

			auto accept = BIT(m_irc_ircsr, IRCSR_IF) // No IRQ
				|| curlevel == 0 // Disabled IRQ level
				|| m_irc_irilr[i] < curlevel // Higher priority
				|| (m_irc_irilr[i] == curlevel && i < curirq); // Same priority + lower interrupt vector
			if (accept) {
				// The IP[5] bit in the Cause register is set to 1 to indicate an interrupt
				// The IP[4:0] field captures the interrupt vector associated with its source
				m_cop0[COP0_Cause] |= (i & 0xf) << 10;
				m_cop0[COP0_Cause] |= CAUSE_IPEX5;
				m_irc_ircsr = (m_irc_irilr[irq] << 8) | irq;
			}
		}
	}
}

template <int N> TIMER_CALLBACK_MEMBER(tx3927_device::update_timer)
{
	if (BIT(m_tmr[N].TMTCR, TMTCR_TMODE, 2) == 3) {
		// Timer not enabled
		m_timer[N]->adjust(attotime::never);
		return;
	}

	if (BIT(m_tmr[N].TMTCR, TMTCR_TCE) && m_tmr[N].TMTRR < m_tmr[N].TMCPRA) {
		// Running the timer at the exact speed it needs to be causes huge issues with performance, so just increase the step for each tick
		m_tmr[N].TMTRR += TX3927_TIMER_DIVISOR;

		if (m_tmr[N].TMTRR > 0xffffff)
			m_tmr[N].TMTRR = 0xffffff;

		//LOGMASKED(LOG_TX39_TMR, "tmr[%d].TMTRR: %d %d | %d\n", N, m_tmr[N].TMTRR, m_tmr[N].TMCPRA, BIT(m_tmr[N].TMITMR, TMITMR_TIIE));
	}

	if (m_tmr[N].TMTRR >= m_tmr[N].TMCPRA) {
		if (BIT(m_tmr[N].TMITMR, TMITMR_TZCE)) {
			m_tmr[N].TMTRR = 0;
		}

		if (BIT(m_tmr[N].TMITMR, TMITMR_TIIE)) {
			// Timer Interval Interrupt Enabled
			trigger_irq(13 + N, ASSERT_LINE);
		}

		m_tmr[N].TMTISR |= 1 << TMTISR_TIIS; // Set interrupt on TIIS
	}
}

uint32_t tx3927_device::tmr_read(offs_t offset, uint32_t mem_mask)
{
	auto tmr_idx = (offset >> 6) & 3;
	auto tmr_offset = (offset & 0x3f) << 2;

	switch (tmr_offset) {
	case 0x00:
		return m_tmr[tmr_idx].TMTCR & 0xff;
	case 0x04:
		return m_tmr[tmr_idx].TMTISR & 0xf;
	case 0x08:
		return m_tmr[tmr_idx].TMCPRA & 0xffffff;
	case 0x0c:
		return m_tmr[tmr_idx].TMCPRB & 0xffffff;
	case 0x10:
		return m_tmr[tmr_idx].TMITMR & 0xffff;
	case 0x20:
		return m_tmr[tmr_idx].TMCCDR & 0x7;
	case 0x30:
		return m_tmr[tmr_idx].TMPGMR & 0xffff;
	case 0x40:
		if (tmr_idx == 2) {
			// Only exists for 3rd timer
			return m_tmr[tmr_idx].TMWTMR & 0xffff;
		}
		break;
	case 0xf0: {
		// Timer Read Register
		return m_tmr[tmr_idx].TMTRR & 0xffffff;
	}
	}

	LOGMASKED(LOG_TX39_TMR, "%s: tmr read %08x %08x\n", machine().describe_context().c_str(), offset * 4, mem_mask);
	return 0;
}

void tx3927_device::tmr_write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	auto tmr_idx = (offset >> 6) & 3;
	auto tmr_offset = (offset & 0x3f) << 2;

	switch (tmr_offset) {
	case 0x00:
		m_tmr[tmr_idx].TMTCR = data & 0xff;

		if (BIT(m_tmr[tmr_idx].TMTCR, TMTCR_TCE) == 0 && BIT(m_tmr[tmr_idx].TMTCR, TMTCR_CRE)) {
			// Disable + reset enabled = zero counter
			LOGMASKED(LOG_TX39_TMR, "Timer %d counter reset\n", tmr_idx);
			m_tmr[tmr_idx].TMTRR = 0;
		}

		update_timer_speed();
		break;
	case 0x04:
		m_tmr[tmr_idx].TMTISR = data & 0xe;

		if (BIT(data, 0) == 0) { // Has no effect when 1 is written
			if (BIT(m_irc_irssr, 13 + tmr_idx)) {
				trigger_irq(13 + tmr_idx, CLEAR_LINE);
			}

			m_tmr[tmr_idx].TMTISR &= ~(1 << 0); // Unset interrupt
		}
		break;
	case 0x08:
		m_tmr[tmr_idx].TMCPRA = data & 0xffffff;
		break;
	case 0x0c:
		m_tmr[tmr_idx].TMCPRB = data & 0xffffff;
		break;
	case 0x10:
		m_tmr[tmr_idx].TMITMR = data & 0xffff;
		break;
	case 0x20:
		m_tmr[tmr_idx].TMCCDR = data & 0x7;
		update_timer_speed();
		break;
	case 0x30:
		m_tmr[tmr_idx].TMPGMR = data & 0xffff;
		break;
	case 0x40:
		if (tmr_idx == 2) {
			m_tmr[tmr_idx].TMWTMR = data & 0xffff;
		}
		break;
	}

	if (offset != 1)
		LOGMASKED(LOG_TX39_TMR, "%s: tmr write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 4, data, mem_mask);
}

uint32_t tx3927_device::ccfg_read(offs_t offset, uint32_t mem_mask)
{
	LOGMASKED(LOG_TX39_CCFG, "%s: ccfg read %08x %08x\n", machine().describe_context().c_str(), offset * 4, mem_mask);
	switch (offset * 4) {
	case 0x00:
		// CCFG
		return m_ccfg;
	case 0x04:
		// CRIR
		return m_crir;
	case 0x08:
		// PCFG
		return m_pcfg;
	case 0x0c:
		// TEAR
		return m_tear;
	case 0x10:
		// PDCR
		return m_pdcr;
	}

	return 0;
}

void tx3927_device::ccfg_write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	LOGMASKED(LOG_TX39_CCFG, "%s: ccfg write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 4, data, mem_mask);

	switch (offset * 4) {
	case 0x00:
		// CCFG
		m_ccfg = (m_ccfg & ~0x3dc01) | (data & 0x3dc01);
		break;
	case 0x08:
		// PCFG
		m_pcfg = data & 0xfffffff;
		break;
	case 0x10:
		// PDCR
		m_pdcr = data & 0xffffff;
		break;
	}
}

uint32_t tx3927_device::sdram_read(offs_t offset, uint32_t mem_mask)
{
	LOGMASKED(LOG_TX39_SDRAM, "%s: sdram_read %08x %08x\n", machine().describe_context().c_str(), offset * 4, mem_mask);

	if ((offset * 4) == 0x20) {
		// SDCTR1
		return 0x400;
	}
	else if ((offset * 4) == 0x24) {
		// SDCTR2
		return 0xff;
	}

	return 0;
}

void tx3927_device::sdram_write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	LOGMASKED(LOG_TX39_SDRAM, "%s: sdram_write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 4, data, mem_mask);
}

uint32_t tx3927_device::rom_read(offs_t offset, uint32_t mem_mask)
{
	LOGMASKED(LOG_TX39_ROM, "%s: rom_read %08x %08x\n", machine().describe_context().c_str(), offset * 4, mem_mask);

	if (offset > 7)
		return 0;

	return m_rom_rccr[offset];
}

void tx3927_device::update_rom_config(int idx)
{
	auto base_addr = BIT(m_rom_rccr[idx], 20, 12) << 20;
	auto bus_width = BIT(m_rom_rccr[idx], 7) ? 1 : 2;
	auto channel_size = std::min(
		int(pow(2, BIT(m_rom_rccr[idx], 8, 4))) * 1024 * 1024 * bus_width,
		0x20000000
	);

	LOGMASKED(LOG_TX39_ROM, "ram[%d]: %08x | %06x | %08x -> %08x\n", idx, m_rom_rccr[idx], channel_size, base_addr, base_addr + channel_size - 1);

	/*
	if (m_ram[idx] && m_ram[idx]->pointer()) {
	    m_program->install_ram(0x00000000 + base_addr, 0x00000000 + base_addr + channel_size - 1, m_ram[idx]->pointer());
	    m_program->install_ram(0x80000000 + base_addr, 0x80000000 + base_addr + channel_size - 1, m_ram[idx]->pointer());
	    m_program->install_ram(0xa0000000 + base_addr, 0xa0000000 + base_addr + channel_size - 1, m_ram[idx]->pointer());
	}
	*/
}

void tx3927_device::rom_write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	LOGMASKED(LOG_TX39_ROM, "%s: rom_write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 4, data, mem_mask);

	if (offset > 7)
		return;

	if (m_rom_rccr[offset] != data) {
		m_rom_rccr[offset] = data;
		update_rom_config(offset);
	}
}

uint32_t tx3927_device::dma_read(offs_t offset, uint32_t mem_mask)
{
	LOGMASKED(LOG_TX39_DMA, "%s: dma_read %08x %08x\n", machine().describe_context().c_str(), offset * 4, mem_mask);
	return 0;
}

void tx3927_device::dma_write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	LOGMASKED(LOG_TX39_DMA, "%s: dma_write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 4, data, mem_mask);
}

uint32_t tx3927_device::irc_read(offs_t offset, uint32_t mem_mask)
{
	uint32_t ret = 0;

	switch (offset * 4) {
	case 0x00:
		// Interrupt Control Enable Register
		ret = m_irc_ircer;
		break;
	case 0x04:
		// Interrupt Control Mode Register 0
		break;
	case 0x08:
		// Interrupt Control Mode Register 1
		break;
	case 0x10:
		// Interrupt Level 0 000000000111 HDD
		// Interrupt Level 1 011100000000 CD-ROM
		ret = m_irc_irilr_full[0];
		break;
	case 0x14:
		// Interrupt Level 2 000000000111
		// Interrupt Level 3 011100000000
		ret = m_irc_irilr_full[1];
		break;
	case 0x18:
		// Interrupt Level 4 000000000111
		// Interrupt Level 5 011100000000
		ret = m_irc_irilr_full[2];
		break;
	case 0x1c:
		// Interrupt Level 6 000000000111
		// Interrupt Level 7 011100000000
		ret = m_irc_irilr_full[3];
		break;
	case 0x20:
		// Interrupt Level 8 000000000111
		// Interrupt Level 9 011100000000
		ret = m_irc_irilr_full[4];
		break;
	case 0x24:
		// Interrupt Level 10 000000000111
		ret = m_irc_irilr_full[5];
		break;
	case 0x28:
		// Interrupt Level 13 011100000000
		ret = m_irc_irilr_full[6];
		break;
	case 0x2c:
		// Interrupt Level 14 000000000111
		// Interrupt Level 15 011100000000
		ret = m_irc_irilr_full[7];
		break;
	case 0x40:
		// Interrupt Mask Level
		ret = m_irc_irimr;
		break;
	case 0x60:
		// Interrupt Status/Control Register
		break;
	case 0x80:
		// Interrupt Source Status Register
		ret = m_irc_irssr;
		break;
	case 0xa0:
		// Interrupt Current Status Register
		ret = m_irc_ircsr;
		break;
	}

	LOGMASKED(LOG_TX39_IRC, "%s: irc_read %08x | %08x\n", machine().describe_context().c_str(), offset * 4, ret);

	return ret;
}

void tx3927_device::irc_write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	LOGMASKED(LOG_TX39_IRC, "%s: irc_write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 4, data, mem_mask);

	switch (offset * 4) {
	case 0x00:
		m_irc_ircer = data;
		break;
	case 0x04:
		for (int i = 0; i < 8; i++)
			m_irc_ircr[i] = BIT(data, i * 2, 2);
		break;
	case 0x08:
		for (int i = 0; i < 8; i++)
			m_irc_ircr[8+i] = BIT(data, i * 2, 2);
		break;
	case 0x10:
		// Interrupt Level 0 000000000111 HDD
		// Interrupt Level 1 011100000000 CD-ROM
		m_irc_irilr_full[0] = data;
		m_irc_irilr[0] = BIT(data, 0, 3);
		m_irc_irilr[1] = BIT(data, 8, 3);
		break;
	case 0x14:
		// Interrupt Level 2 000000000111
		// Interrupt Level 3 011100000000
		m_irc_irilr_full[1] = data;
		m_irc_irilr[2] = BIT(data, 0, 3);
		m_irc_irilr[3] = BIT(data, 8, 3);
		break;
	case 0x18:
		// Interrupt Level 4 000000000111
		// Interrupt Level 5 011100000000
		m_irc_irilr_full[2] = data;
		m_irc_irilr[4] = BIT(data, 0, 3);
		m_irc_irilr[5] = BIT(data, 8, 3);
		break;
	case 0x1c:
		// Interrupt Level 6 000000000111
		// Interrupt Level 7 011100000000
		m_irc_irilr_full[3] = data;
		m_irc_irilr[6] = BIT(data, 0, 3);
		m_irc_irilr[7] = BIT(data, 8, 3);
		break;
	case 0x20:
		// Interrupt Level 8 000000000111
		// Interrupt Level 9 011100000000
		m_irc_irilr_full[4] = data;
		m_irc_irilr[8] = BIT(data, 0, 3);
		m_irc_irilr[9] = BIT(data, 8, 3);
		break;
	case 0x24:
		// Interrupt Level 10 000000000111
		m_irc_irilr_full[5] = data;
		m_irc_irilr[10] = BIT(data, 0, 3);
		m_irc_irilr[11] = BIT(data, 8, 3);
		break;
	case 0x28:
		// Interrupt Level 13 011100000000
		m_irc_irilr_full[6] = data;
		m_irc_irilr[12] = BIT(data, 0, 3);
		m_irc_irilr[13] = BIT(data, 8, 3);
		break;
	case 0x2c:
		// Interrupt Level 14 000000000111
		// Interrupt Level 15 011100000000
		m_irc_irilr_full[7] = data;
		m_irc_irilr[14] = BIT(data, 0, 3);
		m_irc_irilr[15] = BIT(data, 8, 3);
		break;
	case 0x40:
		m_irc_irimr = data;
		break;
	case 0x60:
		m_irc_irscr = data & 0xffffefff;
		if (BIT(data, 8)) {
			auto source = BIT(m_irc_irscr, 0, 4);
			trigger_irq(source, CLEAR_LINE);
		}
		break;
	}
}

uint32_t tx3927_device::pci_read(offs_t offset, uint32_t mem_mask)
{
	auto r = 0;

	switch (offset * 4) {
	case 0x00: {
		// +002 Device ID Register (DID)
		// +000 Vendor ID Register (VID)
		constexpr uint16_t device_id = 0x000a; // TX3927
		constexpr uint16_t vendor_id = 0x102f; // Toshiba
		return (device_id << 16) | vendor_id;
	}

	case 0x04: {
		// +006 PCI Status Register (PCISTAT)
		// +004 PCI Command Register (PCICMD)
		return (m_pci_pcistat << 16) | m_pcicmd;
	}

	case 0x08: {
		// +00b Class Code Register (CC)
		// +00a Subclass Code Register (SCC)
		// +009 Register-Level Programming Interface Register (RLPI)
		// +008 Revision ID Register (RID)
		constexpr uint8_t class_code = 0x06;
		constexpr uint8_t subclass_code = 0x00;
		constexpr uint8_t rlpi = 0x00; // Register-Level Programming Interface
		constexpr uint8_t rev_id = 0; // ?
		return (class_code << 24) | (subclass_code << 16) | (rlpi << 8) | rev_id;
	}

	case 0x0c: {
		// +00e Header Type Register (HT)
		// +00d Master Latency Timer Register (MLT)
		// +00c Cache Line Size Register
		constexpr uint8_t mfht = 0; // Multi-Function and Header Type
		constexpr uint8_t mlt = 0x1f; // Master Latency Timer Count Value
		constexpr uint8_t cls = 0;
		return (mfht << 16) | (mlt << 8) | cls;
	}

	case 0x10: {
		// +010 Target I/O Base Address Register (IOBA)
		constexpr uint8_t imai = 1; //  I/O Base Address Indicator
		return (m_pci_iba << 2) | imai;
	}

	case 0x14: {
		// +014 Target Memory Base Address Register (MBA)
		constexpr uint8_t pf = 1; // Prefetchable
		constexpr uint8_t mty = 0; // Memory Type
		constexpr uint8_t mbai = 0; // Memory Base Address Indicator
		return (m_pci_mba << 4) | (pf << 3) | (mty << 1) | mbai;
	}

	case 0x2c: {
		// +02e System Vendor ID Register (SVID)
		// +02c Subsystem Vendor ID Register (SSVID)
		return (m_pci_svid << 16) | m_pci_ssvid;
	}

	case 0x34: {
		// +037 Capabilities Pointer (CAPPTR)
		constexpr uint8_t capptr = 0xe0;
		return capptr;
	}

	case 0x3c: {
		// +03f Maximum Latency Register (ML)
		// +03e Minimum Grant Register (MG)
		// +03d Interrupt Pin Register (IP)
		// +03c Interrupt Line Register (IL)
		return (m_pci_ml << 24) | (m_pci_mg << 16) | (m_pci_ip << 8) | m_pci_il;
	}

	case 0x44:
		r = m_pci_istat;
		break;

	case 0x154: {
		// Initiator Indirect Data Register (IPCIDATA)
		return m_ipcidata;
	}

	case 0x158: {
		// Initiator Indirect Command/Byte Enable Register (IPCICBE)
		return (m_pci_icmd << 4) | m_pci_ibe;
	}
	}

	LOGMASKED(LOG_TX39_PCI, "%s: pci_read %08x %08x\n", machine().describe_context().c_str(), offset * 4, r);

	return r;
}

void tx3927_device::pci_write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	LOGMASKED(LOG_TX39_PCI, "%s: pci_write %08x %08x\n", machine().describe_context().c_str(), offset * 4, data);

	switch (offset * 4) {
	case 0x04:
		// PCI Status Register (PCISTAT)
		m_pci_pcistat = (m_pci_pcistat & 0x065f) | data;
		break;

	case 0x3c:
		// +03f Maximum Latency Register (ML)
		// +03e Minimum Grant Register (MG)
		// +03d Interrupt Pin Register (IP)
		// +03c Interrupt Line Register (IL)
		m_pci_ml = (data >> 24) & 0xff;
		m_pci_mg = (data >> 16) & 0xff;
		m_pci_ip = (data >> 8) & 0xff;
		m_pci_il = data & 0xff;
		break;

	case 0x44:
		// ISTAT register
		if (BIT(data, 12))
			m_pci_istat &= ~(1 << 12);

		if (BIT(data, 10))
			m_pci_istat &= ~(1 << 10);

		if (BIT(data, 9))
			m_pci_istat &= ~(1 << 9);
		break;

	case 0x128:
		// Local Bus Control Register (LBC)
		m_pci_lbc = data & 0xfffffffc;
		break;

	case 0x148:
		// Initiator Memory Mapping Address Size Register (MMAS)
		m_pci_mmas = data & 0xfffffffc;
		break;

	case 0x14c:
		// Initiator I/O Mapping Address Size Register (IOMAS)
		m_pci_iomas = data & 0xfffffffc;
		break;

	case 0x150:
		// Initiator Indirect Address Register (IPCIADDR)
		m_pci_ipciaddr = data;
		break;

	case 0x154:
		// Initiator Indirect Data Register (IPCIDATA)
		m_pci_ipcidata = data;
		break;

	case 0x158:
		// Initiator Indirect Command/Byte Enable Register (IPCICBE)
		m_pci_icmd = (data >> 4) & 0x0f;
		m_pci_ibe = data & 0x0f;
		m_pci_istat |= 1 << 12;
		break;
	}
}

uint32_t tx3927_device::pio_read(offs_t offset, uint32_t mem_mask)
{
	if (offset != 0)
		LOGMASKED(LOG_TX39_PIO, "%s: pio_read %08x %08x\n", machine().describe_context().c_str(), offset * 4, mem_mask);
	return m_pio_flags[offset];
}

void tx3927_device::pio_write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	if (offset != 0)
		LOGMASKED(LOG_TX39_PIO, "%s: pio_write %08x %08x %08x\n", machine().describe_context().c_str(), offset * 4, data, mem_mask);
	m_pio_flags[offset] = data;
}
