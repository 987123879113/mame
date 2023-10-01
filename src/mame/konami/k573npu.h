// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 Network PCB Unit
 *
 */
#ifndef MAME_KONAMI_K573NPU_H
#define MAME_KONAMI_K573NPU_H

#pragma once

#include "cpu/tx3927/tx3927.h"
#include "bus/ata/ataintf.h"
#include "bus/ata/atapicdr.h"
#include "machine/ds2401.h"
#include "machine/ins8250.h"
#include "machine/ram.h"
#include "machine/timekpr.h"

DECLARE_DEVICE_TYPE(KONAMI_573_NETWORK_PCB_UNIT, k573npu_device)

class k573npu_device : public device_t
{
public:
	k573npu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_add_mconfig(machine_config& config) override;

	void amap(address_map& map);

	virtual const tiny_rom_entry* device_rom_region() const override;

private:
	required_device<ds2401_device> digital_id;
	required_device<tx3927_device> m_maincpu;
	required_device<ram_device> m_ram;

	uint16_t fpgasoft_read(offs_t offset, uint16_t mem_mask);
	void fpgasoft_write(offs_t offset, uint16_t data, uint16_t mem_mask);

	uint16_t fpga_read(offs_t offset, uint16_t mem_mask);
	void fpga_write(offs_t offset, uint16_t data, uint16_t mem_mask);
};

#endif // MAME_KONAMI_K573NPU_H
