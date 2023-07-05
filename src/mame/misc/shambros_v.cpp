// license:BSD-3-Clause
// copyright-holders:windyfairy
#include "emu.h"
#include "shambros_v.h"
#include "screen.h"

// #define VERBOSE (LOG_GENERAL)
// #define LOG_OUTPUT_STREAM std::cout
#include "logmacro.h"

DEFINE_DEVICE_TYPE(SHAMBROS_VIDEO, shambros_video_device, "shambros_video", "Shamisen Brothers Video")

shambros_video_device::shambros_video_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, SHAMBROS_VIDEO, tag, owner, clock)
	, device_video_interface(mconfig, *this)
	, m_flash(*this, ":video_flash")
	, m_ram_obj(*this, "ram_obj")
	, m_ram_pal(*this, "ram_pal")
{
}

void shambros_video_device::device_start()
{
	save_item(NAME(m_flash_write_enable));
	save_item(NAME(m_enabled));
}

void shambros_video_device::device_reset()
{
	m_flash_write_enable = false;
	m_enabled = false;
}

void shambros_video_device::ram_map(address_map &map)
{
	map(0x0000, 0x3fff).ram().share(m_ram_obj); // OBJ RAM
	map(0x8000, 0xffff).ram().share(m_ram_pal); // Palette RAM, 0x200 per palette
}

void shambros_video_device::reg_map(address_map &map)
{
	map(0x14, 0x15).w(FUNC(shambros_video_device::enabled_w));
	map(0x18, 0x19).w(FUNC(shambros_video_device::flash_control_w));
}

void shambros_video_device::data_w(offs_t offset, uint16_t data)
{
	if (m_flash_write_enable)
		m_flash->write(offset, data);
}

uint16_t shambros_video_device::data_r(offs_t offset)
{
	return m_flash->read(offset);
}

void shambros_video_device::flash_control_w(offs_t offset, uint16_t data)
{
	// This might be a control for the video stuff
	// If it's set to 1 then 0xa00000 addresses flash, and if it's 0 then it addresses RAM?
	m_flash_write_enable = data != 0;
}

void shambros_video_device::enabled_w(offs_t offset, uint16_t data)
{
	// Guessed
	m_enabled = data != 0;
}

int shambros_video_device::draw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	bitmap.fill(0, cliprect);

	if (!m_enabled)
		return 0;

	for (int offset = 0; offset < 0x4000; offset += 4) {
		uint16_t x = m_ram_obj[offset] & 0x1ff;
		uint16_t y = m_ram_obj[offset+1] & 0x1ff;
		uint16_t transparency = BIT(m_ram_obj[offset+1], 10, 5);

		uint8_t palidx = BIT(m_ram_obj[offset+2], 0, 6);
		uint8_t tiles_w = BIT(m_ram_obj[offset+2], 8, 3) + 1;
		uint8_t tiles_h = BIT(m_ram_obj[offset+2], 12, 3) + 1;
		// Possibly something relating to rotation or scaling in these unknown registers?
		uint8_t unk1 = BIT(m_ram_obj[offset+2], 6, 2);
		uint8_t unk2 = BIT(m_ram_obj[offset+2], 8, 8) & 0x44; // only take the unknown bits
		uint8_t xflip = BIT(m_ram_obj[offset+2], 11); // these aren't confirmed and don't seem to actually be used, but I think they are flipped bit flags based on the code
		uint8_t yflip = BIT(m_ram_obj[offset+2], 15);

		uint32_t char_offset = BIT(m_ram_obj[offset+3], 0, 14) * 0x100;
		bool is_transparent = BIT(m_ram_obj[offset+3], 14);
		bool is_last = BIT(m_ram_obj[offset+3], 15);

		LOG("%04x: x[%04x] y[%04x] unk1[%02x] unk2[%02x] pal[%02x] w[%02x] h[%02x] xflip[%d] yflip[%d] char_offset[%08x] trans[%d %04x] is_last[%d] | obj[%04x]\n", offset, x, y, unk1, unk2, palidx, tiles_w, tiles_h, xflip, yflip, char_offset, is_transparent, transparency, is_last, m_ram_obj[offset+2]);

		if (is_last) {
			LOG("end of list\n");
			break;
		}

		if (char_offset == 0) // HACK: skip blank tile
			continue;

		if (!cliprect.contains(x, y))
			continue;

		for (int m = 0; m < tiles_h; m++) {
			for (int n = 0; n < tiles_w; n++) {
				for (int i = 0; i < 16; i++) { // y
					for (int j = 0; j < 16; j++) { // x
						const int ty = y + m * 16 + i;
						const int tx = x + (n * 16) + j;

						if (!cliprect.contains(tx, ty)) // skip drawing pixels off screen
							continue;

						uint16_t *const pix = &bitmap.pix(ty, tx);
						const uint32_t offset = char_offset + (m * (0x100 * tiles_w)) + (n * 0x100) + (i * 16) + j;

						if (offset < 0x10000 && palidx == 0) {
							// special handling for what would've been ASCII characters
							// d[0] = 0x7fff; // set to solid white block
							continue;
						}

						const uint16_t val = m_flash->read_raw(offset / 2);
						const int colidx = BIT(val, 8 * (1 - (offset & 1)), 8);
						const uint16_t color = m_ram_pal[palidx * 0x100 + colidx];
						if (colidx != 0) {
							if (is_transparent) {
								const uint8_t sr = BIT(pix[0], 10, 5);
								const uint8_t sg = BIT(pix[0], 5, 5);
								const uint8_t sb = BIT(pix[0], 0, 5);

								const uint8_t r = BIT(color, 10, 5);
								const uint8_t g = BIT(color, 5, 5);
								const uint8_t b = BIT(color, 0, 5);

								const uint8_t nr = std::min<uint8_t>(((sr * (31 - transparency)) + (r * transparency)) >> 5, 31);
								const uint8_t ng = std::min<uint8_t>(((sg * (31 - transparency)) + (g * transparency)) >> 5, 31);
								const uint8_t nb = std::min<uint8_t>(((sb * (31 - transparency)) + (b * transparency)) >> 5, 31);

								pix[0] = (nr << 10) | (ng << 5) | nb;
							} else {
								pix[0] = color;
							}
						}
					}
				}
			}
		}
	}

	return 0;
}
