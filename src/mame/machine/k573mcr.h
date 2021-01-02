// license:BSD-3-Clause
// copyright-holders:smf, windyfairy
/*
 * Konami 573 Memory Card Reader
 *
 */
#ifndef MAME_MACHINE_K573_MCR_H
#define MAME_MACHINE_K573_MCR_H

#pragma once

#include "machine/jvsdev.h"

#include "cpu/mips/mips3.h"

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

    void amap(address_map &map);

	virtual const tiny_rom_entry *device_rom_region() const override;

	// JVS device overrides
	virtual const char *device_id() override;
	virtual uint8_t command_format_version() override;
	virtual uint8_t jvs_standard_version() override;
	virtual uint8_t comm_method_version() override;

private:
    required_device<tx3904be_device> m_maincpu;
};

DECLARE_DEVICE_TYPE(KONAMI_573_MEMORY_CARD_READER, k573mcr_device)

#endif // MAME_MACHINE_K573_MCR_H
