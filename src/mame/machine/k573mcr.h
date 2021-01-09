// license:BSD-3-Clause
// copyright-holders:smf, windyfairy
/*
 * Konami 573 Memory Card Reader
 *
 */
#ifndef MAME_MACHINE_K573_MCR_H
#define MAME_MACHINE_K573_MCR_H

#pragma once

#include "bus/psx/memcard.h" // MAME's build system hurts my brain. Can't get bus/psx building without this
#include "bus/psx/memcard_single.h"
#include "machine/jvsdev.h"

class jvs_host;

class k573mcr_device : public jvs_device
{
public:
	template <typename T>
	k573mcr_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock, T &&jvs_host_tag)
		: k573mcr_device(mconfig, tag, owner, clock)
	{
		host.set_tag(std::forward<T>(jvs_host_tag));
	}

	k573mcr_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
	template <uint8_t First> void set_port_tags() { }

	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_add_mconfig(machine_config &config) override;

	virtual const tiny_rom_entry *device_rom_region() const override;

	// JVS device overrides
	virtual const char *device_id() override;
	virtual uint8_t command_format_version() override;
	virtual uint8_t jvs_standard_version() override;
	virtual uint8_t comm_method_version() override;

	virtual int handle_message_callback(const uint8_t *send_buffer, uint32_t send_size, uint8_t *&recv_buffer) override;

private:
	enum {
		MEMCARD_UNKNOWN     = 0x0000,
		MEMCARD_ERROR       = 0x0002,
		MEMCARD_UNAVAILABLE = 0x0008,
		MEMCARD_READING     = 0x0200,
		MEMCARD_WRITING     = 0x0400,
		MEMCARD_READY       = 0x8000
	};

	uint8_t pcb_buf[65535];
	uint32_t pcb_buf_addr;
	uint32_t controller_port, sec_slot, controller_base_addr;
	uint16_t card_status[2];
	bool is_controller_connected[2];
	uint32_t buf_mode;

	required_device<psxcard_single_device> m_cards[2];

	bool memcard_read(uint32_t port, uint16_t addr, uint8_t *output);
	bool memcard_write(uint32_t port, uint16_t addr, uint8_t *input);
};

DECLARE_DEVICE_TYPE(KONAMI_573_MEMORY_CARD_READER, k573mcr_device)

#endif // MAME_MACHINE_K573_MCR_H
