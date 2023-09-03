// license:BSD-3-Clause
// copyright-holders:Ville Linde
#ifndef MAME_KONAMI_K057714_H
#define MAME_KONAMI_K057714_H

#pragma once


class k057714_device : public device_t, public device_video_interface
{
public:
	// construction/destruction
	k057714_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	void map(address_map &map);

	auto irq_callback() { return m_irq.bind(); }

	void set_pixclock(const XTAL &xtal);

	int draw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	void direct_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	void vblank_w(int state);

	struct framebuffer
	{
		uint32_t base;

		uint16_t width, height;
		uint16_t x, y;

		bool enabled;

		uint8_t brightness[2];
		bool brightness_flags[2];
	};

protected:
	virtual void device_start() override;
	virtual void device_stop() override;
	virtual void device_reset() override;

private:
	enum {
		VRAM_SIZE = 0x2000000,
		FB_PITCH = 1024,
	};

	void crtc_set_screen_params();

	void execute_command(uint32_t *cmd);
	void execute_display_list(uint32_t addr);
	void draw_object(uint32_t *cmd);
	void fill_rect(uint32_t *cmd);
	void draw_character(uint32_t *cmd);
	void fb_config(uint32_t *cmd);

	void draw_frame(int frame, bitmap_ind16 &bitmap, const rectangle &cliprect, bool inverse_trans);

	std::unique_ptr<uint32_t[]> m_vram;
	uint32_t m_vram_read_addr;
	uint32_t m_vram_fifo_addr[2];
	uint32_t m_vram_fifo_mode[2];
	uint32_t m_command_fifo[2][4];
	uint32_t m_command_fifo_ptr[2];

	framebuffer m_display_window[4];
	bool m_display_windows_disabled;

	framebuffer m_window_direct;

	uint32_t m_fb_origin_x;
	uint32_t m_fb_origin_y;
	uint32_t m_priority;

	uint16_t m_mixbuffer;
	uint16_t m_bgcolor;
	uint16_t m_direct_config;
	uint16_t m_unk_reg;

	uint32_t m_display_h_visarea;
	uint32_t m_display_h_frontporch;
	uint32_t m_display_h_backporch;
	uint32_t m_display_h_syncpulse;
	uint32_t m_display_v_visarea;
	uint32_t m_display_v_frontporch;
	uint32_t m_display_v_backporch;
	uint32_t m_display_v_syncpulse;

	uint32_t m_pixclock;

	uint16_t m_irqctrl;

	devcb_write_line m_irq;
};

DECLARE_DEVICE_TYPE(K057714, k057714_device)


#endif // MAME_KONAMI_K057714_H
