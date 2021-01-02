// license:BSD-3-Clause
// copyright-holders:smf
/*
 * Konami 573 Network PCB Unit
 *
 */
#ifndef MAME_MACHINE_K573NPU_H
#define MAME_MACHINE_K573NPU_H

#pragma once

#include "cpu/mips/mips3.h"

DECLARE_DEVICE_TYPE(KONAMI_573_NETWORK_PCB_UNIT, k573npu_device)

class k573npu_device : public device_t
{
public:
	k573npu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
	virtual void device_start() override;
    virtual void device_add_mconfig(machine_config &config) override;

    void amap(address_map &map);

	virtual const tiny_rom_entry *device_rom_region() const override;

private:
    required_device<tx3927be_device> m_maincpu;
};

#endif // MAME_MACHINE_K573NPU_H
