// license:BSD-3-Clause
// copyright-holders:Fabio Priuli, Acho A. Tang, R. Belmont
#ifndef MAME_VIDEO_K037122_H
#define MAME_VIDEO_K037122_H
#pragma once

#include "tilemap.h"

class k037122_device : public device_t,
						public device_video_interface,
						public device_gfx_interface
{
public:
	static constexpr feature_type imperfect_features() { return feature::GRAPHICS; } // unimplemented tilemap ROZ, scroll registers

	k037122_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// configuration
	auto vblank_callback() { return m_vblank_cb.bind(); }
	auto irq_cleared_callback() { return m_irq_cleared_cb.bind(); }
	void set_gfx_index(int index) { m_gfx_index = index; }
	void set_pixclock(const XTAL &xtal);

	void tile_draw( screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect );
	uint32_t sram_r(offs_t offset);
	void sram_w(offs_t offset, uint32_t data, uint32_t mem_mask = ~0);
	uint32_t char_r(offs_t offset);
	void char_w(offs_t offset, uint32_t data, uint32_t mem_mask = ~0);
	uint32_t reg_r(offs_t offset);
	void reg_w(offs_t offset, uint32_t data, uint32_t mem_mask = ~0);

protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;

private:
	// internal state
	tilemap_t* m_tilemap_128 = nullptr;
	tilemap_t* m_tilemap_256 = nullptr;

	uint32_t m_palette_base = 0;
	uint32_t m_tilemap_base = 0;

	uint32_t m_display_h_total, m_display_h_visarea, m_display_h_syncwidth, m_display_h_frontporch, m_display_h_backporch;
	uint32_t m_display_v_total, m_display_v_visarea, m_display_v_syncwidth, m_display_v_frontporch, m_display_v_backporch;
	uint32_t m_pixclock;

	std::unique_ptr<uint32_t[]>       m_tile_ram;
	std::unique_ptr<uint32_t[]>       m_char_ram;
	std::unique_ptr<uint32_t[]>       m_reg;

	int            m_gfx_index;

	void recompute_video_timing();
	void adjust_vblank_start_timer();
	virtual void vblank_start(s32 param);
	virtual void vblank_stop(s32 param);

	TILE_GET_INFO_MEMBER(tile_info);

	emu_timer *m_vsync_start_timer;
	emu_timer *m_vsync_stop_timer;
	devcb_write_line m_vblank_cb;
	devcb_write8 m_irq_cleared_cb;
	bool m_vblank_irq_cleared;
	uint32_t m_vsyncstart, m_vsyncstop;
	attoseconds_t m_frame_period;
};

DECLARE_DEVICE_TYPE(K037122, k037122_device)

#endif // MAME_VIDEO_K037122_H
