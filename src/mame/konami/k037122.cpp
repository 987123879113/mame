// license:BSD-3-Clause
// copyright-holders:Fabio Priuli,Acho A. Tang, R. Belmont
/*
Konami 037122

    This chip has CLUT, CRTC, Variable size single tilemap

    Color lookup table is 16 bit BRG format, total 8192 entries (256 color per each tile * 32 banks))

    Color format (4 byte (1x32bit word) per each color)
    Bits                              Description
    fedcba9876543210 fedcba9876543210
    ---------------- xxxxx----------- Blue
    ---------------- -----xxxxx------ Red
    ---------------- ----------xxxxxx Green

    Tilemap can be rotated, zoomed, similar to K053936 "simple mode"
    Tilemap can be configured either as 256x64 tiles (2048x512 pixels) or 128x128 tiles (1024x1024 pixels), Each tile is 8bpp 8x8.

    Tile format (4 byte (1x32bit word) per each tile)
    Bits                              Description
    fedcba9876543210 fedcba9876543210
    --------x------- ---------------- Flip Y
    ---------x------ ---------------- Flip X
    ----------xxxxx- ---------------- CLUT Bank index (256 color granularity)
    ---------------- --xxxxxxxxxxxxxx Tile code (from character RAM, 8x8 pixel granularity (128 byte))

    Other bits unknown

    Register map
    00-0f Display timing
    20-2f Scroll/ROZ register
    30-3f Control, etc

    Offset Bits                              Description
           fedcba9876543210 fedcba9876543210
    00     xxxxxxxxxxxxxxxx ---------------- Horizontal total pixels - 1
           ---------------- xxxxxxxxxxxxxxxx Horizontal sync width - 1
    04     xxxxxxxxxxxxxxxx ---------------- Horizontal front porch + 1 (horizontal total pixels - horizontal back porch - horizontal sync width - horizontal width + 1)
           ---------------- xxxxxxxxxxxxxxxx Horizontal back porch - 1
    08     xxxxxxxxxxxxxxxx ---------------- Vertical total pixels - 1
           ---------------- xxxxxxxxxxxxxxxx Vertical sync width - 1
    0c     xxxxxxxxxxxxxxxx ---------------- Vertical front porch (vertical total pixels - vertical back porch - vertical sync width - vertical width)
           ---------------- xxxxxxxxxxxxxxxx Vertical back porch - 1
    20     sxxxxxxxxxxxxxxx ---------------- X counter starting value (12.4 fixed point)
           ---------------- sxxxxxxxxxxxxxxx Y counter starting value (12.4 fixed point)
    24     sxxxxxxxxxxxxxxx ---------------- amount to add to the X counter after each line (4.12 fixed point)
           ---------------- sxxxxxxxxxxxxxxx amount to add to the Y counter after each line (4.12 fixed point)
    28     sxxxxxxxxxxxxxxx ---------------- amount to add to the X counter after each horizontal pixel (4.12 fixed point)
           ---------------- sxxxxxxxxxxxxxxx amount to add to the Y counter after each horizontal pixel (4.12 fixed point)
    30     ---------------x ---------------- Tilemap size
           ---------------0 ---------------- 128x128
           ---------------1 ---------------- 256x64
           ---------------- -------------xxx Character RAM bank
    34     ---------------- -------------x-- CLUT location
           ---------------- -------------0-- CLUT at 0x00000-0x08000
           ---------------- -------------1-- CLUT at 0x18000-0x20000
           ---------------x ---------------- Tilemap Y origin
           ---------------0 ---------------- Origin at tilemap center
           ---------------1 ---------------- Origin at 0

    Other bits/registers unknown, some registers are used

TODO:
    - verify and implement display timing registers
    - verify other unknown but used registers
*/

#include "emu.h"
#include "k037122.h"
#include "konami_helper.h"
#include "screen.h"

//#define VERBOSE 1
#include "logmacro.h"


#define K037122_NUM_TILES       16384

DEFINE_DEVICE_TYPE(K037122, k037122_device, "k037122", "K037122 2D Tilemap")

k037122_device::k037122_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, K037122, tag, owner, clock),
	device_video_interface(mconfig, *this),
	device_gfx_interface(mconfig, *this, nullptr),
	m_tile_ram(nullptr),
	m_char_ram(nullptr),
	m_reg(nullptr),
	m_gfx_index(0),
	m_vsync_start_timer(nullptr),
	m_vblank_cb(*this),
	m_irq_cleared_cb(*this)
{
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void k037122_device::device_start()
{
	if (!palette().device().started())
		throw device_missing_dependencies();

	m_vblank_cb.resolve();
	m_irq_cleared_cb.resolve();

	static const gfx_layout k037122_char_layout =
	{
	8, 8,
	K037122_NUM_TILES,
	8,
	{ 0,1,2,3,4,5,6,7 },
	{ 1*16, 0*16, 3*16, 2*16, 5*16, 4*16, 7*16, 6*16 },
	{ 0*128, 1*128, 2*128, 3*128, 4*128, 5*128, 6*128, 7*128 },
	8*128
	};

	m_char_ram = make_unique_clear<uint32_t[]>(0x200000 / 4);
	m_tile_ram = make_unique_clear<uint32_t[]>(0x20000 / 4);
	m_reg = make_unique_clear<uint32_t[]>(0x400 / 4);

	m_tilemap_128 = &machine().tilemap().create(*this, tilemap_get_info_delegate(*this, FUNC(k037122_device::tile_info)), TILEMAP_SCAN_ROWS, 8, 8, 128, 128);
	m_tilemap_256 = &machine().tilemap().create(*this, tilemap_get_info_delegate(*this, FUNC(k037122_device::tile_info)), TILEMAP_SCAN_ROWS, 8, 8, 256, 64);

	m_tilemap_128->set_transparent_pen(0);
	m_tilemap_256->set_transparent_pen(0);

	set_gfx(m_gfx_index,std::make_unique<gfx_element>(&palette(), k037122_char_layout, (uint8_t*)m_char_ram.get(), 0, palette().entries() / 16, 0));

	save_pointer(NAME(m_reg), 0x400 / 4);
	save_pointer(NAME(m_char_ram), 0x200000 / 4);
	save_pointer(NAME(m_tile_ram), 0x20000 / 4);
	save_item(NAME(m_tilemap_base));
	save_item(NAME(m_palette_base));

	save_item(NAME(m_display_h_total));
	save_item(NAME(m_display_h_visarea));
	save_item(NAME(m_display_h_syncwidth));
	save_item(NAME(m_display_h_frontporch));
	save_item(NAME(m_display_h_backporch));
	save_item(NAME(m_display_v_total));
	save_item(NAME(m_display_v_visarea));
	save_item(NAME(m_display_v_syncwidth));
	save_item(NAME(m_display_v_frontporch));
	save_item(NAME(m_display_v_backporch));
	save_item(NAME(m_pixclock));

	m_vsync_start_timer = timer_alloc(FUNC(k037122_device::vblank_start),this);
	m_vsync_stop_timer = timer_alloc(FUNC(k037122_device::vblank_stop),this);
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void k037122_device::device_reset()
{
	memset(m_char_ram.get(), 0, 0x200000);
	memset(m_tile_ram.get(), 0, 0x20000);
	memset(m_reg.get(), 0, 0x400);

	m_tilemap_base = 0;
	m_palette_base = 0;

	m_vblank_irq_cleared = true;

	// Sane defaults used by Hornet games as the 24kHz 512x384 configuration
	m_display_h_visarea = 512;
	m_display_h_total = 644;
	m_display_h_syncwidth = 40;
	m_display_h_backporch = 48;
	m_display_h_frontporch = m_display_h_total - m_display_h_backporch - m_display_h_syncwidth - m_display_h_visarea;
	m_display_v_visarea = 384;
	m_display_v_total = 428;
	m_display_v_syncwidth = 6;
	m_display_v_backporch = 26;
	m_display_v_frontporch = m_display_v_total - m_display_v_backporch - m_display_v_syncwidth - m_display_v_visarea;
	m_pixclock = XTAL(50'000'000).value() / 3;
	recompute_video_timing();
}

inline void k037122_device::recompute_video_timing()
{
	m_display_h_visarea = m_display_h_total - m_display_h_syncwidth - m_display_h_backporch - m_display_h_frontporch;
	m_display_v_visarea = m_display_v_total - m_display_v_syncwidth - m_display_v_backporch - m_display_v_frontporch;
	m_vsyncstart = m_display_v_syncwidth - m_display_v_backporch;
	m_vsyncstop = m_display_v_backporch;

	logerror(
		"%d %d %d %d %d | %d %d %d %d %d | %d\n",
		m_display_h_visarea, m_display_h_total, m_display_h_syncwidth, m_display_h_backporch, m_display_h_frontporch,
		m_display_v_visarea, m_display_v_total, m_display_v_syncwidth, m_display_v_backporch, m_display_v_frontporch,
		m_pixclock
	);

	// Currently the k037122 is only used by Hornet which also has a Voodoo hooked up
	// which sets the screen parameters again after this.
	rectangle visarea(0, m_display_h_visarea - 1, 0, m_display_v_visarea - 1);
	screen().configure(m_display_h_total, m_display_v_total, visarea, HZ_TO_ATTOSECONDS(m_pixclock) * m_display_h_total * m_display_v_total);

	adjust_vblank_start_timer();
}

void k037122_device::set_pixclock(const XTAL &xtal)
{
	xtal.validate(std::string("Setting pixel clock for ") + tag());

	auto new_pixclock = xtal.value();
	if (new_pixclock != m_pixclock)
	{
		m_pixclock = new_pixclock;
		recompute_video_timing();
	}
}

void k037122_device::adjust_vblank_start_timer()
{
	attotime time_until_blank = screen().time_until_pos(m_vsyncstart);

	// if zero, adjust to next frame, otherwise we may get stuck in an infinite loop
	if (time_until_blank == attotime::zero)
		time_until_blank = screen().frame_period();

	m_vsync_start_timer->adjust(time_until_blank);
}

void k037122_device::vblank_start(s32 param)
{
	m_vsync_stop_timer->adjust(screen().time_until_pos(m_vsyncstop));

	m_vblank_irq_cleared = false;
	if (!m_vblank_cb.isnull())
		m_vblank_cb(true);
}

void k037122_device::vblank_stop(s32 param)
{
	if (!m_vblank_cb.isnull())
		m_vblank_cb(false);

	adjust_vblank_start_timer();
}

/*****************************************************************************
    DEVICE HANDLERS
*****************************************************************************/

TILE_GET_INFO_MEMBER(k037122_device::tile_info)
{
	uint32_t val = m_tile_ram[tile_index + (m_tilemap_base / 4)];
	int color = (val >> 17) & 0x1f;
	int tile = val & 0x3fff;
	int flags = 0;

	if (val & 0x400000)
		flags |= TILE_FLIPX;
	if (val & 0x800000)
		flags |= TILE_FLIPY;

	tileinfo.set(m_gfx_index, tile, color, flags);
}


void k037122_device::tile_draw( screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect )
{
	const rectangle &visarea = screen.visible_area();

	int16_t scrollx = m_reg[0x8] >> 16;
	int16_t scrolly = m_reg[0x8] & 0xffff;

	int16_t incxx = m_reg[0xa] >> 16;
	int16_t incxy = m_reg[0xa] & 0xffff;
	int16_t incyx = m_reg[0x9] >> 16;
	int16_t incyy = m_reg[0x9] & 0xffff;

	if (m_reg[0xc] & 0x10000)
	{
		if ((m_reg[0xd] & 0x10000) == 0)
			scrolly -= 0x2000;

		m_tilemap_128->set_scrolldx(visarea.min_x, visarea.min_x);
		m_tilemap_128->set_scrolldy(visarea.min_y, visarea.min_y);

		m_tilemap_128->draw_roz(screen, bitmap, cliprect, (int32_t)(scrollx) << 12, (int32_t)(scrolly) << 12,
			(int32_t)(incxx) << 4, (int32_t)(incxy) << 4, (int32_t)(incyx), (int32_t)(incyy) << 4,
			false, 0, 0);
	}
	else
	{
		if ((m_reg[0xd] & 0x10000) == 0)
			scrolly -= 0x1000;

		m_tilemap_256->set_scrolldx(visarea.min_x, visarea.min_x);
		m_tilemap_256->set_scrolldy(visarea.min_y, visarea.min_y);

		m_tilemap_256->draw_roz(screen, bitmap, cliprect, (int32_t)(scrollx) << 12, (int32_t)(scrolly) << 12,
			(int32_t)(incxx) << 4, (int32_t)(incxy) << 4, (int32_t)(incyx) << 4, (int32_t)(incyy) << 4,
			false, 0, 0);
	}

}

uint32_t k037122_device::sram_r(offs_t offset)
{
	return m_tile_ram[offset];
}

void k037122_device::sram_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	COMBINE_DATA(m_tile_ram.get() + offset);

	uint32_t address = offset * 4;

	if (address >= m_palette_base && address < m_palette_base + 0x8000)
	{
		int color = (address - m_palette_base) / 4;
		palette().set_pen_color(color, pal5bit(data >> 6), pal6bit(data >> 0), pal5bit(data >> 11));
	}

	if (address >= m_tilemap_base && address < m_tilemap_base + 0x10000)
	{
		m_tilemap_128->mark_tile_dirty((address - m_tilemap_base) / 4);
		m_tilemap_256->mark_tile_dirty((address - m_tilemap_base) / 4);
	}
}


uint32_t k037122_device::char_r(offs_t offset)
{
	int bank = m_reg[0x30 / 4] & 0x7;

	return m_char_ram[offset + (bank * (0x40000 / 4))];
}

void k037122_device::char_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	int bank = m_reg[0x30 / 4] & 0x7;
	uint32_t addr = offset + (bank * (0x40000/4));

	COMBINE_DATA(m_char_ram.get() + addr);
	gfx(m_gfx_index)->mark_dirty(addr / 32);
}

uint32_t k037122_device::reg_r(offs_t offset)
{
	switch (offset)
	{
		case 0x14/4:
		{
			return 0x000003fa;
		}
	}
	return m_reg[offset];
}

void k037122_device::reg_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	switch (offset)
	{
		case 0x00:
			if (ACCESSING_BITS_16_31)
			{
				m_display_h_total = ((data >> 16) & 0xffff) + 1;
			}
			if (ACCESSING_BITS_0_15)
			{
				m_display_h_syncwidth = (data & 0xffff) + 1;
			}
			recompute_video_timing();
			break;

		case 0x04/4:
			if (ACCESSING_BITS_16_31)
			{
				m_display_h_frontporch = ((data >> 16) & 0xffff) - 1;
			}
			if (ACCESSING_BITS_0_15)
			{
				m_display_h_backporch = (data & 0xffff) + 1;
			}
			recompute_video_timing();
			break;

		case 0x08/4:
			if (ACCESSING_BITS_16_31)
			{
				m_display_v_total = ((data >> 16) & 0xffff) + 1;
			}
			if (ACCESSING_BITS_0_15)
			{
				m_display_v_syncwidth = (data & 0xffff) + 1;
			}
			recompute_video_timing();
			break;

		case 0x0c/4:
			if (ACCESSING_BITS_16_31)
			{
				m_display_v_frontporch = (data >> 16) & 0xffff;
			}
			if (ACCESSING_BITS_0_15)
			{
				m_display_v_backporch = (data & 0xffff) + 1;
			}
			recompute_video_timing();
			break;

		case 0x10/4:
			if (ACCESSING_BITS_0_15)
			{
				// This is cleared when IRQ0 (main screen IRQ) is called in Hornet games
				if (m_vblank_irq_cleared && !m_irq_cleared_cb.isnull()) {
					m_vblank_irq_cleared = true;
					m_irq_cleared_cb(0);
				}
			}
			break;

		case 0x14/4:
			if (ACCESSING_BITS_16_31)
			{
				// This is cleared when IRQ1 (sub screen IRQ) is called in Hornet games
				if (m_vblank_irq_cleared && !m_irq_cleared_cb.isnull()) {
					m_vblank_irq_cleared = true;
					m_irq_cleared_cb(1);
				}
			}
			break;

		case 0x34/4:
			if (ACCESSING_BITS_0_15)
			{
				uint32_t palette_base = (data & 0x4) ? 0x18000 : 0x00000;

				// tilemap is at 0x00000 unless CLUT is there
				uint32_t tilemap_base = (data & 0x4) ? 0x00000 : 0x08000;

				if (palette_base != m_palette_base)
				{
					m_palette_base = palette_base;

					// update all colors since palette moved
					for (auto p = 0; p < 8192; p++)
					{
						uint32_t color = m_tile_ram[(m_palette_base / 4) + p];
						palette().set_pen_color(p, pal5bit(color >> 6), pal6bit(color >> 0), pal5bit(color >> 11));
					}
				}

				if (tilemap_base != m_tilemap_base)
				{
					m_tilemap_128->mark_all_dirty();
					m_tilemap_256->mark_all_dirty();
					m_tilemap_base = tilemap_base;
				}
			}
			break;
	}

	COMBINE_DATA(m_reg.get() + offset);
}
