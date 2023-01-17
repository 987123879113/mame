// license:BSD-3-Clause
// copyright-holders:Luca Elia,David Haywood,Paul Priest,windyfairy
/*
    Jaleco VJ sprite hardware

	Based on the Megasystem 32 sprite hardware (ms32_sprite.cpp) with being that this chip works with YUV
	data and the sprites are directly fed into the Qtaro cards which mixes the in-game videos with the sprites layer.

	Implemented via the FPGAs on the subboard for Stepping Stage and VJ.
*/

#include "emu.h"
#include "vj_sprite.h"
#include "drawgfxt.ipp"

DEFINE_DEVICE_TYPE(JALECO_VJ_SPRITE, vj_sprite_device, "vjspr", "Jaleco VJ Sprite hardware")

vj_sprite_device::vj_sprite_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, JALECO_VJ_SPRITE, tag, owner, clock)
	, device_gfx_interface(mconfig, *this, nullptr)
	, m_color_base(0)
	, m_color_entries(0x10)
{
}

/*************************************
 *
 *  Graphics definitions
 *
 *************************************/

/* sprites are contained in 256x256 "tiles" */
static const u32 sprite_xoffset[256] =
{
	STEP8(8*8*8*0,    8), STEP8(8*8*8*1,    8), STEP8(8*8*8*2,    8), STEP8(8*8*8*3,    8),
	STEP8(8*8*8*4,    8), STEP8(8*8*8*5,    8), STEP8(8*8*8*6,    8), STEP8(8*8*8*7,    8),
	STEP8(8*8*8*8,    8), STEP8(8*8*8*9,    8), STEP8(8*8*8*10,   8), STEP8(8*8*8*11,   8),
	STEP8(8*8*8*12,   8), STEP8(8*8*8*13,   8), STEP8(8*8*8*14,   8), STEP8(8*8*8*15,   8),
	STEP8(8*8*8*16,   8), STEP8(8*8*8*17,   8), STEP8(8*8*8*18,   8), STEP8(8*8*8*19,   8),
	STEP8(8*8*8*20,   8), STEP8(8*8*8*21,   8), STEP8(8*8*8*22,   8), STEP8(8*8*8*23,   8),
	STEP8(8*8*8*24,   8), STEP8(8*8*8*25,   8), STEP8(8*8*8*26,   8), STEP8(8*8*8*27,   8),
	STEP8(8*8*8*28,   8), STEP8(8*8*8*29,   8), STEP8(8*8*8*30,   8), STEP8(8*8*8*31,   8)
};
static const u32 sprite_yoffset[256] =
{
	STEP8(8*8*8*0,  8*8), STEP8(8*8*8*32, 8*8), STEP8(8*8*8*64, 8*8), STEP8(8*8*8*96, 8*8),
	STEP8(8*8*8*128,8*8), STEP8(8*8*8*160,8*8), STEP8(8*8*8*192,8*8), STEP8(8*8*8*224,8*8),
	STEP8(8*8*8*256,8*8), STEP8(8*8*8*288,8*8), STEP8(8*8*8*320,8*8), STEP8(8*8*8*352,8*8),
	STEP8(8*8*8*384,8*8), STEP8(8*8*8*416,8*8), STEP8(8*8*8*448,8*8), STEP8(8*8*8*480,8*8),
	STEP8(8*8*8*512,8*8), STEP8(8*8*8*544,8*8), STEP8(8*8*8*576,8*8), STEP8(8*8*8*608,8*8),
	STEP8(8*8*8*640,8*8), STEP8(8*8*8*672,8*8), STEP8(8*8*8*704,8*8), STEP8(8*8*8*736,8*8),
	STEP8(8*8*8*768,8*8), STEP8(8*8*8*800,8*8), STEP8(8*8*8*832,8*8), STEP8(8*8*8*864,8*8),
	STEP8(8*8*8*896,8*8), STEP8(8*8*8*928,8*8), STEP8(8*8*8*960,8*8), STEP8(8*8*8*992,8*8)
};
static const gfx_layout spritelayout =
{
	256, 256,
	RGN_FRAC(1,1),
	8,
	{ STEP8(0,1) },
	EXTENDED_XOFFS,
	EXTENDED_YOFFS,
	256*256*8,
	sprite_xoffset,
	sprite_yoffset
};

GFXDECODE_MEMBER(vj_sprite_device::gfxinfo)
	GFXDECODE_DEVICE(DEVICE_SELF, 0, spritelayout, 0, 16)
GFXDECODE_END

void vj_sprite_device::device_start()
{
	// decode our graphics
	decode_gfx(gfxinfo);
	gfx(0)->set_colorbase(m_color_base);
	gfx(0)->set_colors(m_color_entries);
}

void vj_sprite_device::extract_parameters(const u16 *ram, bool &disable, u32 &code, u32 &color, u8 &tx, u8 &ty, u16 &srcwidth, u16 &srcheight, s32 &sx, s32 &sy)
{
	assert((ram[0] & 3) == 0);

	disable   = BIT(ram[0], 2) == 0;
	color     = BIT(ram[0], 8, 7);
	tx        = BIT(ram[1], 0, 8);
	ty        = BIT(ram[1], 8, 8);
	code      = BIT(ram[2], 0, 12);
	srcwidth  = BIT(ram[3], 0, 8) + 1;
	srcheight = BIT(ram[3], 8, 8) + 1;
	sy        = BIT(ram[4], 0, 9) - (ram[4] & 0x200);
	sx        = BIT(ram[5], 0, 10) - (ram[5] & 0x400);

	// if (!disable)
	// 	printf("%04x | %d %d %d %d %d %d\n", color, tx, ty, srcwidth, srcheight, sy, sx);
}

void vj_sprite_device::render(u32 *output, u32 code, u32 color, s32 destx, s32 desty, u32 tx, u32 ty, u32 srcwidth, u32 srcheight, u16 *paletteram)
{
	g_profiler.start(PROFILER_DRAWGFX);

	do {
		assert(code < gfx(0)->elements());

		const int destendx = destx + srcwidth - 1;
		int srcx = 0;

		const int destendy = desty + srcheight - 1;
		int srcy = 0;

		// fetch the source data
		const u8 *srcdata = gfx(0)->get_data(code);

		// compute how many blocks of 4 pixels we have
		u32 numblocks = destendx + 1 - destx;

		// iterate over pixels in Y
		for (s32 cury = desty; cury <= destendy; cury++)
		{
			u32 drawy = ty + srcy;
			srcy++;

			if (drawy >= gfx(0)->height())
				continue;

			auto *destptr = &output[(cury * 2) * 720 + (destx * 2)];
			auto *destptr2 = &output[((cury * 2) + 1) * 720 + (destx * 2)];
			const u8 *srcptr = srcdata + (drawy * gfx(0)->rowbytes());

			for (s32 curx = srcx; curx < numblocks; curx++)
			{
				if (tx + curx < gfx(0)->width()) {
					// if (srcptr[tx + curx] != 0)
					// 	printf("%04x %04x %04x\n", color, srcptr[tx + curx], color * 0x200 + srcptr[tx + curx] * 4);

					auto pal = &paletteram[(color * 0x100 + srcptr[tx + curx]) * 4];
					destptr[0] = destptr2[0] = (pal[0] & 0xff) | ((pal[1] & 0xff) << 8) | ((pal[2] & 0xff) << 16);
					destptr[1] = destptr2[1] = (pal[0] & 0xff) | ((pal[3] & 0xff) << 8) | ((pal[2] & 0xff) << 16);

					assert(destptr[0] == destptr[0]);
					assert(destptr2[0] == destptr2[0]);

					destptr++;
					destptr2++;
					destptr++;
					destptr2++;
				} else {
					destptr++;
					destptr2++;
					destptr++;
					destptr2++;
				}
			}
		}
	} while (0);

	g_profiler.stop();
}
