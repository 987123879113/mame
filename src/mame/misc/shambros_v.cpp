// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
    Video chip for Shamisen Brothers (Kato's PCB)

    Internal object mapping has the following fields:
    HPOS VPOS CHAR CH CL FL FD SZ PR GP -BLD-  STR
    +08 pointer to string (if it's a string)
    +0c hpos -> screen x
    +0e vpos -> screen y
    +10 char
    +12 ch
    +13 cl -> related to palette. palette index?
    +14 fl -> flags. 0x40 = is a string
    +15 fd -> ?
    +16 sz -> tile size = (val+1) * 8
    +17 pr -> priority?
    +18 gp

    What gets mapped in OBJ RAM is:
    +00 hpos, max 0x1ff
    +02 vpos, max 0x1ff
    +04 tile size + flags
        b_yya_xx
        xx and yy are the bits used for the width and height of the tiles, (val+1) * 8 = tile size
        a (0x08) and b (0x80) are flags for mirror? inversion? the calculation of hpos and vpos changes to (-basepos)+tilesize instead of just basepos+tilesize
    +05 ? (TODO: research cl in internal data)
    +06 combined palette + char? 0x4000 is a bit for some flag, 0x8000 is disabled?
*/

#include "emu.h"
#include "shambros_v.h"
#include "screen.h"

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
}

void shambros_video_device::device_reset()
{
	m_flash_write_enable = false;
	m_enabled = false;
}

void shambros_video_device::device_stop()
{
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

	// printf("%s data_w %08x %04x\n", machine().describe_context().c_str(), offset, data);
}

uint16_t shambros_video_device::data_r(offs_t offset)
{
	// printf("%s data_r %08x\n", machine().describe_context().c_str(), offset);
	return m_flash->read(offset);
}

void shambros_video_device::flash_control_w(offs_t offset, uint16_t data)
{
	// This might be a control for the video stuff
	// 0x810018 is used the same as how 0x500024/0x500026 are used
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
		// uint8_t unk1 = BIT(m_ram_obj[offset+2], 6, 2);
		// uint8_t unk2 = BIT(m_ram_obj[offset+2], 8, 8) & 0x44; // only take the unknown bits
		uint8_t tiles_w = BIT(m_ram_obj[offset+2], 8, 3) + 1;
		uint8_t tiles_h = BIT(m_ram_obj[offset+2], 12, 3) + 1;
		// uint8_t xflip = BIT(m_ram_obj[offset+2], 11); // these aren't confirmed and don't seem to actually be used, but I think they are flipped bit flags based on the code
		// uint8_t yflip = BIT(m_ram_obj[offset+2], 15);

		uint32_t char_offset = BIT(m_ram_obj[offset+3], 0, 14) * 0x100;
		bool is_transparent = BIT(m_ram_obj[offset+3], 14);
		bool is_last = BIT(m_ram_obj[offset+3], 15);

		// printf("%04x: x[%04x] y[%04x] unk1[%02x] unk2[%02x] pal[%02x] w[%02x] h[%02x] xflip[%d] yflip[%d] char_offset[%08x] trans[%d %04x] is_last[%d] | obj[%04x]\n", offset, x, y, unk1, unk2, palidx, tiles_w, tiles_h, xflip, yflip, char_offset, is_transparent, transparency, is_last, m_ram_obj[offset+2]);

		if (is_last) {
			// printf("\n");
			break;
		}

		if (char_offset == 0) // HACK: skip blank tile
			continue;

		if (x > bitmap.width() || y > bitmap.height()) // RAM test?
			continue;

		for (int m = 0; m < tiles_h; m++) {
			for (int i = 0; i < tiles_w; i++) {
				for (int k = 0; k < 16; k++) { // y
					for (int j = 0; j < 16; j++) { // x
						auto ty = y + m * 16 + k;
						auto tx = x + (i * 16) + j;

						if (!cliprect.contains(tx, ty)) // skip drawing pixels off screen
							continue;

						uint16_t *const d = &bitmap.pix(ty, tx);
						const uint32_t o = char_offset + (m * (0x100 * tiles_w)) + (i * 0x100) + (k * 16) + j;

						if (o < 0x10000 && palidx == 0) {
							// d[0] = 0x7fff;
							continue;
						}

						auto v = m_flash->read_raw(o >> 1);
						auto c = BIT(v, 8 * ((o & 1) ? 0 : 1), 8);
						if (c != 0) {
							if (is_transparent) {
								double sr = ((d[0] >> 10) & 0x1f) / 31.0;
								double sg = ((d[0] >>  5) & 0x1f) / 31.0;
								double sb = ((d[0] >>  0) & 0x1f) / 31.0;
								double r = ((m_ram_pal[palidx * 0x100 + c] >> 10) & 0x1f) / 31.0;
								double g = ((m_ram_pal[palidx * 0x100 + c] >>  5) & 0x1f) / 31.0;
								double b = ((m_ram_pal[palidx * 0x100 + c] >>  0) & 0x1f) / 31.0;
								double alpha1 = transparency / 31.0;
								double alpha2 = 1.0 - alpha1;

								uint8_t nr = std::min<uint8_t>(((sr * alpha2) + (r * alpha1)) * 31, 31);
								uint8_t ng = std::min<uint8_t>(((sg * alpha2) + (g * alpha1)) * 31, 31);
								uint8_t nb = std::min<uint8_t>(((sb * alpha2) + (b * alpha1)) * 31, 31);

								d[0] = (nr << 10) | (ng << 5) | nb;
							} else {
								d[0] = m_ram_pal[palidx * 0x100 + c];
							}
						}
					}
				}
			}
		}
	}

	return 0;
}
