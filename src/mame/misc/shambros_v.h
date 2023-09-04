// license:BSD-3-Clause
// copyright-holders:windyfairy
#ifndef MAME_MISC_SHAMBROS_V_H
#define MAME_MISC_SHAMBROS_V_H

#pragma once

#include "machine/intelfsh.h"

class shambros_video_device : public device_t, public device_video_interface
{
public:
	shambros_video_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	void ram_map(address_map &map);
	void reg_map(address_map &map);

	int draw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	void data_w(offs_t offset, uint16_t data);
	uint16_t data_r(offs_t offset);

protected:
	virtual void device_start() override;
	virtual void device_reset() override;

private:
	void flash_control_w(offs_t offset, uint16_t data);
	void enabled_w(offs_t offset, uint16_t data);

	required_device<intelfsh16_device> m_flash;

	required_shared_ptr<uint16_t> m_ram_obj;
	required_shared_ptr<uint16_t> m_ram_pal;

	bool m_flash_write_enable;
	bool m_enabled;
};

DECLARE_DEVICE_TYPE(SHAMBROS_VIDEO, shambros_video_device)


#endif // MAME_MISC_SHAMBROS_V_H
