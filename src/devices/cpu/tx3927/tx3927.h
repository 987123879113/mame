// license:BSD-3-Clause
// copyright-holders:windyfairy

#ifndef MAME_CPU_MIPS_TX3927_H
#define MAME_CPU_MIPS_TX3927_H

#pragma once

#include "cpu/mips/mips1.h"
#include "cpu/tx3927/sio.h"
#include "machine/ram.h"

class tx3927_device : public mips1_device_base
{
public:
	tx3927_device(machine_config const& mconfig, char const* tag, device_t* owner, u32 clock, size_t icache_size = 8192, size_t dcache_size = 4096);

	void trigger_irq(int irq, int state);

protected:
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_add_mconfig(machine_config& config) override;

	virtual space_config_vector memory_space_config() const override;

	template <int N> TIMER_CALLBACK_MEMBER(update_timer);
	emu_timer* m_timer[3];

	template <int N> void ata_interrupt(int state);
	void ata_dmarq(int state);

	void amap(address_map& map);

	address_space_config const m_program_config;
	address_space *m_program;

private:
	enum cause_mask : u32
	{
		CAUSE_EXCCODE = 0x0000007c, // exception code
		CAUSE_IPSW0   = 0x00000100, // software interrupt 0 pending
		CAUSE_IPSW1   = 0x00000200, // software interrupt 1 pending
		CAUSE_IPEX0   = 0x00000400, // external interrupt 0 pending
		CAUSE_IPEX1   = 0x00000800, // external interrupt 1 pending
		CAUSE_IPEX2   = 0x00001000, // external interrupt 2 pending
		CAUSE_IPEX3   = 0x00002000, // external interrupt 3 pending
		CAUSE_IPEX4   = 0x00004000, // external interrupt 4 pending
		CAUSE_IPEX5   = 0x00008000, // external interrupt 5 pending
		CAUSE_IP      = 0x0000ff00, // interrupt pending
		CAUSE_CE      = 0x30000000, // co-processor error
		CAUSE_BD      = 0x80000000, // branch delay

		CAUSE_IPEX    = 0x0000fc00, // external interrupt pending
	};

	enum cop0_reg : u8
	{
		COP0_Index    = 0,
		COP0_Random   = 1,
		COP0_EntryLo  = 2,
		COP0_BusCtrl  = 2,  // r3041 only
		COP0_Config   = 3,  // r3041/r3071/r3081 only
		COP0_Context  = 4,
		COP0_BadVAddr = 8,
		COP0_Count    = 9,  // r3041 only
		COP0_EntryHi  = 10,
		COP0_PortSize = 10, // r3041 only
		COP0_Compare  = 11, // r3041 only
		COP0_Status   = 12,
		COP0_Cause    = 13,
		COP0_EPC      = 14,
		COP0_PRId     = 15,
	};

	required_device_array<tx3927_sio, 2> m_sio;

	void update_timer_speed();
	void update_rom_config(int idx);

	uint32_t tmr_read(offs_t offset, uint32_t mem_mask);
	void tmr_write(offs_t offset, uint32_t data, uint32_t mem_mask);

	uint32_t ccfg_read(offs_t offset, uint32_t mem_mask);
	void ccfg_write(offs_t offset, uint32_t data, uint32_t mem_mask);

	uint32_t sdram_read(offs_t offset, uint32_t mem_mask);
	void sdram_write(offs_t offset, uint32_t data, uint32_t mem_mask);

	uint32_t rom_read(offs_t offset, uint32_t mem_mask);
	void rom_write(offs_t offset, uint32_t data, uint32_t mem_mask);

	uint32_t dma_read(offs_t offset, uint32_t mem_mask);
	void dma_write(offs_t offset, uint32_t data, uint32_t mem_mask);

	uint32_t irc_read(offs_t offset, uint32_t mem_mask);
	void irc_write(offs_t offset, uint32_t data, uint32_t mem_mask);

	uint32_t pci_read(offs_t offset, uint32_t mem_mask);
	void pci_write(offs_t offset, uint32_t data, uint32_t mem_mask);

	uint32_t pio_read(offs_t offset, uint32_t mem_mask);
	void pio_write(offs_t offset, uint32_t data, uint32_t mem_mask);

	// ROM
	uint32_t m_rom_rccr[8];

	// TMR
	enum : u32 {
		TMTCR_TCE = 7, // Timer Count Enable
		TMTCR_CCDE = 6, // Counter Clock Divide Enable
		TMTCR_CRE = 5, // Counter Reset Enable
		TMTCR_ECES = 3, // External Clock Edge Select
		TMTCR_CCS = 2, // Counter Clock Select
		TMTCR_TMODE = 0, // Timer Mode

		TMITMR_TIIE = 15, // Timer Interval Interrupt Enable
		TMITMR_TZCE = 0, // Interval Timer Zero Clear Enable

		TMTISR_TWIS = 3, // Timer Watchdog Interrupt Status
		TMTISR_TPIBS = 2, // Timer Pulse Generator Interrupt by TMCPRB Status
		TMTISR_TIPAS = 1, // Timer Pulse Generator Interrupt by TMCPRA Status
		TMTISR_TIIS = 0, // Timer Interval Interrupt Status
	};

	typedef struct {
		uint32_t TMTCR;  // 0x00 Timer Control Register
		uint32_t TMTISR; // 0x04 Timer Interrupt Status Register
		uint32_t TMCPRA; // 0x08 Compare Register A
		uint32_t TMCPRB; // 0x0c Compare Register B
		uint32_t TMITMR; // 0x10 Interval Timer Mode Register
		uint32_t TMCCDR; // 0x20 Clock Divider Register
		uint32_t TMPGMR; // 0x30 Pulse Generator Mode Register
		uint32_t TMWTMR; // 0x40 Watchdog Timer Mode Register
		uint32_t TMTRR;  // 0xf0 Timer Read Register
	} TMR;
	TMR m_tmr[3] = {};

	// IRC
	enum : u32 {
		IRCSR_IF = 16, // Interrupt Flag
		IRCSR_ILV = 8, // Interrupt Level Vector
		IRCSR_IVL = 0, // Interrupt Vector
	};

	uint32_t m_irc_irssr;  // Interrupt Source Status Register
	uint32_t m_irc_irscr; // Interrupt Status/Control Register
	uint32_t m_irc_ircsr; // Interrupt Current Status Register
	uint32_t m_irc_ircer;
	uint32_t m_irc_irimr;
	uint32_t m_irc_irilr[16];
	uint32_t m_irc_irilr_full[8];
	uint32_t m_irc_ircr[16];

	// CCFG
	uint32_t m_ccfg;
	uint32_t m_crir;
	uint32_t m_pcfg;
	uint32_t m_tear;
	uint32_t m_pdcr;

	// PIO
	uint32_t m_pio_flags[64] = {};

	// PCIC
	uint32_t m_pci_istat;
	uint16_t m_pci_pcistat;
	uint16_t m_pcicmd;
	uint32_t m_pci_iba; // I/O Space Base Address
	uint32_t m_pci_mba; // Memory Base Address
	uint16_t m_pci_svid; // System Vendor ID
	uint16_t m_pci_ssvid; // Subsystem Vendor ID
	uint8_t m_pci_ml; // Maximum Latency
	uint8_t m_pci_mg; // Minimum Grant
	uint8_t m_pci_ip; // Interrupt Pin
	uint8_t m_pci_il; // Interrupt Line
	uint32_t m_ipcidata; // Initiator Indirect Data
	uint8_t m_pci_icmd; // Initiator Indirect Command
	uint8_t m_pci_ibe; // Initiator Indirect Byte Enable
	uint32_t m_pci_lbc; // Local Bus Control Register (LBC)
	uint32_t m_pci_mmas; // Initiator Memory Mapping Address Size
	uint32_t m_pci_iomas; // Initiator IO Mapping Address Size
	uint32_t m_pci_ipciaddr; // Initiator Indirect Address
	uint32_t m_pci_ipcidata; // Initiator Indirect Data
};

DECLARE_DEVICE_TYPE(TX3927,      tx3927_device)

#endif // MAME_CPU_MIPS_TX3927_H
