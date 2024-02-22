// license:BSD-3-Clause
// copyright-holders:smf
/*
 * Konami 573 Multi Session Unit
 *
 */
#ifndef MAME_KONAMI_K573MSU_H
#define MAME_KONAMI_K573MSU_H

#pragma once

#include "cpu/tx3927/tx3927.h"
#include "sound/tc9446f.h"



DECLARE_DEVICE_TYPE(KONAMI_573_MULTI_SESSION_UNIT, k573msu_device)

class k573msu_device : public device_t
{
public:
	k573msu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
	virtual void device_start() override;
	virtual void device_add_mconfig(machine_config &config) override;

	virtual const tiny_rom_entry *device_rom_region() const override;

private:
	required_device<tx3927_device> m_maincpu;
	required_device_array<tc9446f_device, 4> m_dsp;
};

#endif // MAME_KONAMI_K573MSU_H
