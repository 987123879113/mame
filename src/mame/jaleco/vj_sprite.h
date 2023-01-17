// license:BSD-3-Clause
// copyright-holders:Luca Elia,David Haywood,Paul Priest,windyfairy
/*
    Jaleco VJ sprite hardware
*/

#ifndef MAME_VIDEO_VJ_SPRITE_H
#define MAME_VIDEO_VJ_SPRITE_H

#pragma once

class vj_sprite_device : public device_t, public device_gfx_interface
{
public:
	// construction
	vj_sprite_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	// configuration
	void set_color_base(u16 base) { m_color_base = base; }
	void set_color_entries(u16 entries) { m_color_entries = entries; }

	void extract_parameters(const u16 *ram, bool &disable, u32 &code, u32 &color, u8 &tx, u8 &ty, u16 &srcwidth, u16 &srcheight, s32 &sx, s32 &sy);

	void render(u32 *output, u32 code, u32 color, s32 destx, s32 desty, u32 tx, u32 ty, u32 srcwidth, u32 srcheight, u16 *paletteram);

protected:
	// device_t overrides
	virtual void device_start() override;

private:
	// decoding info
	DECLARE_GFXDECODE_MEMBER(gfxinfo);

	// configurations
	u16 m_color_base, m_color_entries;
};

DECLARE_DEVICE_TYPE(JALECO_VJ_SPRITE, vj_sprite_device)

#endif  // MAME_VIDEO_VJ_SPRITE_H
