// license:BSD-3-Clause
// copyright-holders:Ville Linde
/*
Konami 0000057714 "GCU" 2D Graphics Chip


TODO:
- Firebeat games should show a colorful screen on boot but it's unclear exactly how that works.
Might be related to bit 5 of reg 0x7a which is still unknown.
Having that bit set allows for the code to bounce the window base offset around which causes the
exact same glitchy effect as seen in real PCB videos.

- [tropchnc] sets the visible area to 641x481 so there's a line of garbage visible at the bottom and right.
firebeat and konendev games write 639x479 (or the appropriate visible area - 1) to the same registers.
Programming error with tropchnc?


Notes:
- konendev games have a bunch of named functions using the GCU for reference
- Tropical Chance has GCU priority, scaling, and sprite movement tests in the bootloader's test menu (must boot without CD inserted to access)


All GCU-based games on all hardware I've seen so far have shared a common set of constants to reference specific features of the GCU.
You can reference the list below to know what data you're working with easily.

These are used generically as constants for the overall whole thing (surface, FIFO, etc) instead of a specific feature or functionality:
0x42c020 - Primary Surface (m_display_window[0])
0x42c021 - HW1 Surface (m_display_window[1])
0x42c022 - HW2 Surface (m_display_window[2])
0x42c023 - BG surface? (m_display_window[3])
0x42c033 - Unknown surface
0x42c029 - FIFO 0
0x42c02a - FIFO 1

These constants are used in specific contexts/features:
0x42c024 - Used to reference bit 4 of priority register
0x42c025 - Used to reference bit 0 of reg 0x0c
0x42c026 - Used to reference bit 1 of reg 0x0c
0x42c027 - Used to reference bit 2 of reg 0x0c
0x42c028 - Used to reference bit 3 of reg 0x0c
0x42c02b - Primary Surface brightness 1 (used when reading/writing reg 0x14 bits 2-6)
0x42c02c - Primary Surface brightness 2 (used when reading/writing reg 0x14 bits 7-11)
0x42c02d - HW1 Surface brightness 1
0x42c02e - HW1 Surface brightness 2
0x42c02f - HW2 Surface brightness 1
0x42c030 - HW2 Surface brightness 2
0x42c031 - Unknown surface brightness 1
0x42c032 - Unknown surface brightness 2
0x42c034 - Used to reference bit 0 of reg 0x1c. Gets set to open port for direct writes?
0x42c035 - Used to reference bit 1 of reg 0x1c
0x42c036 - Used to reference bit 2 of reg 0x1c
0x42c037 - Used to reference bit 3 of reg 0x1c
0x42c038 - Used to reference bit 4 of reg 0x1c
0x42c039 - Used to reference bit 5 of reg 0x1c
0x42c03a - Used to reference bit 6 of reg 0x1c
*/

#include "emu.h"
#include "k057714.h"
#include "screen.h"


#define DUMP_VRAM 0

#define LOG_REGISTER (1U << 1)
#define LOG_FIFO     (1U << 2)
#define LOG_CMDEXEC  (1U << 3)
#define LOG_DRAW     (1U << 4)
#define LOG_DISPLAY  (1U << 5)
// #define VERBOSE      (LOG_GENERAL | LOG_REGISTER | LOG_FIFO | LOG_CMDEXEC | LOG_DRAW | LOG_DISPLAY)
// #define VERBOSE      (LOG_GENERAL | LOG_REGISTER | LOG_CMDEXEC | LOG_DRAW | LOG_DISPLAY)

#include "logmacro.h"

#define LOGREGISTER(...) LOGMASKED(LOG_REGISTER, __VA_ARGS__)
#define LOGFIFO(...) LOGMASKED(LOG_FIFO, __VA_ARGS__)
#define LOGCMDEXEC(...) LOGMASKED(LOG_CMDEXEC, __VA_ARGS__)
#define LOGDRAW(...) LOGMASKED(LOG_DRAW, __VA_ARGS__)
#define LOGDISPLAY(...) LOGMASKED(LOG_DISPLAY, __VA_ARGS__)

DEFINE_DEVICE_TYPE(K057714, k057714_device, "k057714", "K057714 GCU")

k057714_device::k057714_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, K057714, tag, owner, clock)
	, device_video_interface(mconfig, *this)
	, m_irqctrl(0)
	, m_irq(*this)
{
}

void k057714_device::device_start()
{
	m_vram = std::make_unique<uint32_t[]>(VRAM_SIZE / 4);

	save_pointer(NAME(m_vram), VRAM_SIZE / 4);
	save_item(NAME(m_vram_read_addr));
	save_item(NAME(m_vram_fifo_addr));
	save_item(NAME(m_vram_fifo_mode));
	save_item(NAME(m_command_fifo));
	save_item(NAME(m_command_fifo_ptr));
	save_item(NAME(m_display_windows_disabled));
	save_item(NAME(m_fb_origin_x));
	save_item(NAME(m_fb_origin_y));
	save_item(NAME(m_display_h_visarea));
	save_item(NAME(m_display_h_frontporch));
	save_item(NAME(m_display_h_backporch));
	save_item(NAME(m_display_h_syncpulse));
	save_item(NAME(m_display_v_visarea));
	save_item(NAME(m_display_v_frontporch));
	save_item(NAME(m_display_v_backporch));
	save_item(NAME(m_display_v_syncpulse));
	save_item(NAME(m_pixclock));
	save_item(NAME(m_irqctrl));
	save_item(NAME(m_priority));
	save_item(NAME(m_mixbuffer));
	save_item(NAME(m_direct_config));
	save_item(NAME(m_unk_reg));

	save_item(STRUCT_MEMBER(m_display_window, enabled));
	save_item(STRUCT_MEMBER(m_display_window, base));
	save_item(STRUCT_MEMBER(m_display_window, width));
	save_item(STRUCT_MEMBER(m_display_window, height));
	save_item(STRUCT_MEMBER(m_display_window, x));
	save_item(STRUCT_MEMBER(m_display_window, y));
	save_item(STRUCT_MEMBER(m_display_window, brightness));
	save_item(STRUCT_MEMBER(m_display_window, brightness_flags));

	save_item(STRUCT_MEMBER(m_window_direct, enabled));
	save_item(STRUCT_MEMBER(m_window_direct, base));
	save_item(STRUCT_MEMBER(m_window_direct, width));
	save_item(STRUCT_MEMBER(m_window_direct, height));
	save_item(STRUCT_MEMBER(m_window_direct, x));
	save_item(STRUCT_MEMBER(m_window_direct, y));
}

void k057714_device::device_reset()
{
	/*
	Default display width/height are a guess.
	All Firebeat games except beatmania III, which uses 640x480, will set the
	display width/height through registers.
	The assumption here is that since beatmania III doesn't set the display width/height
	then the game is assuming that it's already at the correct settings upon boot.

	Timing information taken from table found in all Firebeat games.
	table idx (h vis area, front porch, sync pulse, back porch, h total) (v vis area, front porch, sync pulse, back porch, v total)
	0 (640, 16, 96, 48 = 800) (480, 10, 2, 33 = 525)
	1 (512, 5, 96, 72 = 685) (384, 6, 4, 22 = 416)
	2 (800, 40, 128, 88 = 1056) (600, 1, 4, 23 = 628)
	3 (640, 20, 23, 165 = 848) (384, 6, 1, 27 = 418)
	4 (640, 10, 21, 10 = 681) (480, 10, 2, 33 = 525)
	*/
	m_display_h_visarea = 640;
	m_display_h_frontporch = 16;
	m_display_h_backporch = 48;
	m_display_h_syncpulse = 96;
	m_display_v_visarea = 480;
	m_display_v_frontporch = 10;
	m_display_v_backporch = 33;
	m_display_v_syncpulse = 2;
	m_pixclock = 25'175'000; // 25.175_MHz_XTAL, default for Firebeat but maybe not other machines. The value can be changed externally
	crtc_set_screen_params();

	m_vram_read_addr = 0;
	std::fill(std::begin(m_vram_fifo_addr), std::end(m_vram_fifo_addr), 0);
	std::fill(std::begin(m_vram_fifo_mode), std::end(m_vram_fifo_mode), 0);
	std::fill(std::begin(m_command_fifo_ptr), std::end(m_command_fifo_ptr), 0);

	for (int i = 0; i < std::size(m_command_fifo); i++)
		std::fill(std::begin(m_command_fifo[i]), std::end(m_command_fifo[i]), 0);

	m_irqctrl = 0;
	m_mixbuffer = 0;
	m_bgcolor = 0;
	m_direct_config = 0;
	m_unk_reg = 0;

	m_display_windows_disabled = false;

	m_fb_origin_x = 0;
	m_fb_origin_y = 0;

	for (auto & elem : m_display_window)
	{
		elem.enabled = false;
		elem.base = 0;
		elem.width = elem.height = 0;
		elem.x = elem.y = 0;
		std::fill(std::begin(elem.brightness), std::end(elem.brightness), 16);
		std::fill(std::begin(elem.brightness_flags), std::end(elem.brightness_flags), false);
	}

	m_window_direct.enabled = false;
	m_window_direct.base = 0;
	m_window_direct.width = m_window_direct.height = 0;
	m_window_direct.x = m_window_direct.y = 0;

	memset(m_vram.get(), 0, VRAM_SIZE);
}

void k057714_device::device_stop()
{
	if (DUMP_VRAM)
	{
		const std::string filename = util::string_format("%s_vram.bin", basetag());

		FILE *file = fopen(filename.c_str(), "wb");

		if (!file)
			fatalerror("couldn't open %s for writing\n", filename);

		LOG("dumping %s\n", filename);

		for (int i = 0; i < VRAM_SIZE / 4; i++)
		{
			fputc(BIT(m_vram[i], 24, 8), file);
			fputc(BIT(m_vram[i], 16, 8), file);
			fputc(BIT(m_vram[i], 8, 8), file);
			fputc(BIT(m_vram[i], 0, 8), file);
		}

		fclose(file);
	}
}

void k057714_device::map(address_map &map)
{
	map(0x00, 0x01).lw16(NAME([this] (uint16_t data) {
		LOGREGISTER("%s: reg 0x00 %02x\n", machine().describe_context(), data);
		m_display_h_visarea = data + 1;
		crtc_set_screen_params();
	}));

	map(0x02, 0x03).lw16(NAME([this] (uint16_t data) {
		LOGREGISTER("%s: reg 0x02 %02x\n", machine().describe_context(), data);
		m_display_h_frontporch = BIT(data, 8, 8) + 1;
		m_display_h_backporch = BIT(data, 0, 8) + 1;
		crtc_set_screen_params();
	}));

	map(0x04, 0x05).lw16(NAME([this] (uint16_t data) {
		LOGREGISTER("%s: reg 0x04 %02x\n", machine().describe_context(), data);
		m_display_v_visarea = data + 1;
		crtc_set_screen_params();
	}));

	map(0x06, 0x07).lw16(NAME([this] (uint16_t data) {
		LOGREGISTER("%s: reg 0x06 %02x\n", machine().describe_context(), data);
		m_display_v_frontporch = BIT(data, 8, 8) + 1;
		m_display_v_backporch = BIT(data, 0, 8) + 1;
		crtc_set_screen_params();
	}));

	map(0x08, 0x09).lw16(NAME([this] (uint16_t data) {
		LOGREGISTER("%s: reg 0x08 %02x\n", machine().describe_context(), data);
		m_display_h_syncpulse = BIT(data, 8, 8) + 1;
		m_display_v_syncpulse = BIT(data, 0, 8) + 1;
		crtc_set_screen_params();
	}));

	map(0x0a, 0x0b).lw16(NAME([this] (uint16_t data) {
		// Usage unknown but is set to 0x64 during initialization
		LOGREGISTER("%s: reg 0x0a %02x\n", machine().describe_context(), data);
	}));

	map(0x0c, 0x0d).lw16(NAME([this] (uint16_t data) {
		// Usage unknown but is set during initialization for some games
		// konendev games set bit 2 to zero on initialization, bits are never set to 1
		// tropchnc sets bits 0 and 1 to 0 on initialization, never sets anything to 1
		LOGREGISTER("%s: reg 0x0c %02x\n", machine().describe_context(), data);
	}));

	map(0x0e, 0x0f).lw16(NAME([this] (uint16_t data) {
		// Values are typically 1, 2, 2, 6, 2
		const int val1 = BIT(data, 13);
		const int val2 = BIT(data, 10, 3);
		const int val3 = BIT(data, 8, 2);
		const int val4 = BIT(data, 4, 4);
		const int val5 = BIT(data, 1, 3);
		const int val6 = BIT(data, 0); // Only value to get set separately in a second write after the previous values are set. Some kind of enable bit?
		LOGREGISTER("%s: reg 0x0e: %04X %d %d %d %d %d %d\n", machine().describe_context(), data, val1, val2, val3, val4, val5, val6);
		m_unk_reg = data;
	}));

	map(0x10, 0x11).lw16(NAME([this] (uint16_t data) {
		// bit 1 also gets checked and seems IRQ-related but I'm not sure what it does
		LOGREGISTER("%s: reg 0x10 set to %04x\n", machine().describe_context(), data);

		if (!BIT(data, 0))
			m_irq(CLEAR_LINE);

		m_irqctrl = data;
	}));

	map(0x12, 0x13).lw16(NAME([this] (uint16_t data) {
		LOGREGISTER("%s: reg 0x12 %02x\n", machine().describe_context(), data);

		m_priority = data;

		for (int i = 0; i < 4; i++)
			m_display_window[i].enabled = BIT(data, 3 - i) == 1;

		m_display_windows_disabled = BIT(data, 4);
	}));

	map(0x14, 0x1b).lw16(NAME([this] (offs_t offset, uint16_t data) {
		LOGREGISTER("%s: reg 0x14 %02x\n", machine().describe_context(), data);

		/*
		Called "brightness" in a few games
		Some games animate one of the values instead of the other and it's inconsistent which,
		and the flags can be false even if the brightness is meant to be adjusted,
		so it's not an enable/disable flag.
		*/
		for (int i = 0; i < 2; i++)
		{
			m_display_window[offset].brightness[i] = BIT(data, i * 5 + 2, 5);
			m_display_window[offset].brightness_flags[i] = BIT(data, i) == 1;
		}
	}));

	map(0x1c, 0x1d).lw16(NAME([this] (uint16_t data) {
		LOGREGISTER("%s: reg 0x1c %02x\n", machine().describe_context(), data);

		// set to 1 on "media bus" access
		if (BIT(data, 0) && !BIT(m_direct_config, 0))
		{
			m_window_direct.x = 0;
			m_window_direct.y = 0;
		}

		m_window_direct.enabled = BIT(data, 0);
		m_direct_config = data;
	}));

	map(0x1e, 0x1f).lw16(NAME([this] (uint16_t data) {
		LOGREGISTER("%s: reg 0x1e %02x\n", machine().describe_context(), data);

		/*
		Set during "BG color init" in Keyboardmania's boot process.
		A color in the normal format is written here.
		Some Firebeat bootloaders set this to 0x1ce7 (#070707) but all games set it back to 0 after booting.
		*/
		m_bgcolor = data;
	}));

	map(0x20, 0x2f).lw32(NAME([this] (offs_t offset, uint32_t data) {
		LOGREGISTER("%s: reg 0x%02x %02x\n", machine().describe_context(), 0x20 + offset * 2, data);
		m_display_window[offset].x = BIT(data, 0, 16);
		m_display_window[offset].y = BIT(data, 16, 16);
	}));

	map(0x30, 0x3f).lw16(NAME([this] (offs_t offset, uint16_t data) {
		LOGREGISTER("%s: reg 0x%02x %02x\n", machine().describe_context(), 0x30 + offset * 2, data);
		if (offset & 1)
			m_display_window[offset >> 1].width = data;
		else
			m_display_window[offset >> 1].height = data;
	}));

	map(0x40, 0x4f).lw32(NAME([this] (offs_t offset, uint32_t data) {
		LOGREGISTER("%s: reg 0x%02x %02x\n", machine().describe_context(), 0x40 + offset * 4, data);
		m_display_window[offset].base = data;
	}));

	map(0x50, 0x53).lw32(NAME([this] (uint32_t data) {
		LOGREGISTER("%s: reg 0x50 %02x\n", machine().describe_context(), data);
		m_window_direct.x = BIT(data, 0, 16);
		m_window_direct.y = BIT(data, 16, 16);
	}));

	map(0x54, 0x57).lw16(NAME([this] (offs_t offset, uint16_t data) {
		LOGREGISTER("%s: reg 0x%02x %02x\n", machine().describe_context(), 0x54 + offset * 2, data);
		if (offset & 1)
			m_window_direct.width = data;
		else
			m_window_direct.height = data;
	}));

	map(0x58, 0x5b).lw32(NAME([this] (uint32_t data) {
		LOGREGISTER("%s: reg 0x58 %02x\n", machine().describe_context(), data);
		m_window_direct.base = data;
	}));

	map(0x5c, 0x5f).lw32(NAME([this] (uint32_t data) {
		LOGFIFO("%s: reg 0x5c %02x\n", machine().describe_context(), data);
		m_vram_read_addr = data >> 1;
	}));

	map(0x60, 0x67).lw32(NAME([this] (offs_t offset, uint32_t data) {
		LOGFIFO("%s: reg 0x%02x %02x\n", machine().describe_context(), 0x60 + offset * 4, data);
		m_vram_fifo_addr[offset] = data >> 1;
	}));

	map(0x68, 0x6b).lw16(NAME([this] (offs_t offset, uint16_t data) {
		LOGFIFO("%s: reg 0x%02x %02x\n", machine().describe_context(), 0x68 + offset * 2, data);
		m_vram_fifo_mode[offset] = data;
	}));

	map(0x6c, 0x6d).lw16(NAME([this] (uint16_t data) {
		// Unknown, initialized to 0 during boot in some games
		LOGREGISTER("%s: reg 0x6c %02x\n", machine().describe_context(), data);
	}));

	map(0x6e, 0x6f).lw16(NAME([this] (uint16_t data) {
		LOGREGISTER("%s: reg 0x6e %02x\n", machine().describe_context(), data);

		/*
		Called the "mixbuffer" in Keyboardmania's boot process
		Gets set using a hardcoded value during boot process.
		kbm sets this to 0x030b
		bm3final sets this to 0x0fff
		ppp sets this to 0x0020
		*/
		m_mixbuffer = data;
	}));

	map(0x70, 0x77).lw32(NAME([this] (offs_t offset, uint32_t data) {
		// LOGREGISTER("%s: reg 0x%02x %02x\n", machine().describe_context(), 0x70 + offset * 4, data);

		if (m_vram_fifo_mode[offset] & 0x100)
		{
			// write to command fifo
			m_command_fifo[offset][m_command_fifo_ptr[offset]] = data;
			m_command_fifo_ptr[offset]++;

			// execute when filled
			if (m_command_fifo_ptr[offset] >= 4)
			{
				LOGFIFO("GCU FIFO%d exec: %08X %08X %08X %08X\n", offset, m_command_fifo[offset][0], m_command_fifo[offset][1], m_command_fifo[offset][2], m_command_fifo[offset][3]);
				execute_command(m_command_fifo[offset]);
				m_command_fifo_ptr[offset] = 0;
			}
		}
		else
		{
			// write to VRAM fifo
			m_vram[m_vram_fifo_addr[offset]] = data;
			m_vram_fifo_addr[offset]++;
		}
	}));

	map(0x78, 0x79).lr16(NAME([this] () -> uint16_t {
		// Related to IRQs?
		// kbm checks bits 0 and 1
		return m_irqctrl;
	}));

	map(0x7a, 0x7b).lr16(NAME([this] () -> uint16_t {
		uint32_t r = 0;

		/*
		Bits 0 and 2 are checked before sending more commands to the FIFO
		Bits 1 and 3 are also related to FIFO 0 and 1 status but not sure what exactly
		*/
		if (m_command_fifo_ptr[0] < 4)
			r |= 1;

		if (m_command_fifo_ptr[1] < 4)
			r |= 4;

		/*
		Some kind of busy status flag relating to direct access port operations?
		The way this bit is used:
		1) loop while reg 0x7a bit 4 is set to 1
		2) set reg 0x5c address
		3) loop while reg 0x7a bit 4 is set to 1
		4) read 0x80 bytes of data from VRAM
		*/
		r &= ~0x10;

		// Another busy flag
		r &= ~0x20;

		return r;
	}));

	map(0x80, 0xff).lr32(NAME([this] (offs_t offset) -> uint32_t {
		return m_vram[m_vram_read_addr + offset];
	}));
}

void k057714_device::set_pixclock(const XTAL &xtal)
{
	xtal.validate(std::string("Setting pixel clock for ") + tag());
	m_pixclock = xtal.value();
	crtc_set_screen_params();
}

inline void k057714_device::crtc_set_screen_params()
{
	auto htotal = m_display_h_visarea + m_display_h_frontporch + m_display_h_backporch + m_display_h_syncpulse;
	auto vtotal = m_display_v_visarea + m_display_v_frontporch + m_display_v_backporch + m_display_v_syncpulse;

	rectangle visarea(0, m_display_h_visarea - 1, 0, m_display_v_visarea - 1);
	screen().configure(htotal, vtotal, visarea, HZ_TO_ATTOSECONDS(m_pixclock) * htotal * vtotal);
}

void k057714_device::vblank_w(int state)
{
	if (state && (m_irqctrl & 1))
		m_irq(ASSERT_LINE);
}

void k057714_device::direct_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	// Bit 0 if the config register must have been set to 1 for this to be open
	if (!m_window_direct.enabled)
		return;

	uint16_t *vram16 = (uint16_t*)m_vram.get();

	if (m_window_direct.x > 0) // first write of every new line is a dummy write
	{
		const uint32_t addr = m_window_direct.base + (m_window_direct.y * FB_PITCH) + (m_window_direct.x - 1);
		vram16[addr ^ NATIVE_ENDIAN_VALUE_LE_BE(1,0)] = data;
	}
	m_window_direct.x++;

	if (m_window_direct.x > m_window_direct.width + 1)
	{
		m_window_direct.y++;
		m_window_direct.x = 0;
	}
}

void k057714_device::draw_frame(int frame, bitmap_ind16 &bitmap, const rectangle &cliprect, bool inverse_trans)
{
	// if (m_display_window[frame].height == 0 || m_display_window[frame].width == 0)
	// 	return;

	int height = m_display_window[frame].height + 1;
	int width = m_display_window[frame].width + 1;

	uint16_t trans_value = inverse_trans ? 0x8000 : 0x0000;

	/*
	There are often times where the base window (3) is enabled but has a width of 0 while the height is set properly.
	It seems to be a valid case but I am not sure exactly how it works.
	For example, popn8 has only window 3 enabled with the width set to 0 during boot where you'd normally see the color test pattern.
	*/

	if (m_display_window[frame].height == 0 || m_display_window[frame].y + height > cliprect.max_y)
		height = cliprect.max_y - m_display_window[frame].y;

	if (m_display_window[frame].width == 0 || m_display_window[frame].x + width > cliprect.max_x)
		width = cliprect.max_x - m_display_window[frame].x;

	uint16_t *vram16 = (uint16_t*)m_vram.get();

	for (int j = 0; j < height; j++)
	{
		uint16_t *const d = &bitmap.pix(j + m_display_window[frame].y, m_display_window[frame].x);

		int li = (j * FB_PITCH);

		for (int i = 0; i < width; i++)
		{
			uint16_t pix = vram16[(m_display_window[frame].base + li + i) ^ NATIVE_ENDIAN_VALUE_LE_BE(1,0)];

			if ((pix & 0x8000) != trans_value)
			{
				uint8_t r = BIT(pix, 10, 5);
				uint8_t g = BIT(pix, 5, 5);
				uint8_t b = BIT(pix, 0, 5);

				// kbm uses brightness[0] but bm3final uses brightness[1]
				r = (r * m_display_window[frame].brightness[0]) >> 4;
				g = (g * m_display_window[frame].brightness[0]) >> 4;
				b = (b * m_display_window[frame].brightness[0]) >> 4;

				r = (r * m_display_window[frame].brightness[1]) >> 4;
				g = (g * m_display_window[frame].brightness[1]) >> 4;
				b = (b * m_display_window[frame].brightness[1]) >> 4;

				uint16_t fr = std::clamp<uint16_t>(r, 0, 0x1f);
				uint16_t fg = std::clamp<uint16_t>(g, 0, 0x1f);
				uint16_t fb = std::clamp<uint16_t>(b, 0, 0x1f);

				d[i] = (fr << 10) | (fg << 5) | fb;
			}
		}
	}
}

int k057714_device::draw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	bitmap.fill(0, cliprect);

	if (m_display_windows_disabled)
		return 0;

	bool inverse_trans = (m_mixbuffer & 0x0f) && m_mixbuffer != 0xfff;

	if (m_priority != 0)
		LOGDISPLAY("%s draw %04x %04x %04x %04x | cliprect[%d %d %d %d]\n", basetag(), m_priority, m_mixbuffer, m_unk_reg, m_bgcolor, cliprect.min_x, cliprect.min_y, cliprect.max_x, cliprect.max_y);

	for (int i = 0; i < 4; i++)
	{
		const auto window = BIT(m_priority, 8 + i * 2, 2);

		const auto idx = 3 - window;
		const auto enabled = BIT(m_priority, idx);

		if (m_priority != 0)
		{
			const auto mixbuf_val1 = BIT(m_mixbuffer, 4 + idx * 2, 2);
			const auto mixbuf_val2 = BIT(m_mixbuffer, idx);

			LOGDISPLAY("\t%d: window[%d] enabled[%d] mix[%d %d] base[%08x %d %d] fbinfo[%d x %d (%d, %d)] alpha1[%d %d] alpha2[%d %d]\n",
				i, window, enabled, mixbuf_val2, mixbuf_val1,
				m_display_window[window].base, m_display_window[window].base & 0x3ff, (m_display_window[window].base >> 10) & 0x3fff,
				m_display_window[window].width, m_display_window[window].height, m_display_window[window].x, m_display_window[window].y,
				m_display_window[window].brightness_flags[0], m_display_window[window].brightness[0],
				m_display_window[window].brightness_flags[1], m_display_window[window].brightness[1]
			);
		}

		if (enabled)
			draw_frame(window, bitmap, cliprect, inverse_trans);
	}


	return 0;
}

void k057714_device::draw_object(uint32_t *cmd)
{
	// 0x00: xxx----- -------- -------- --------   command (always 5)
	// 0x00: ---x---- -------- -------- --------   0: absolute coordinates
	//                                             1: relative coordinates from framebuffer origin
	// 0x00: -------- xxxxxxxx xxxxxx-- --------   ram y
	// 0x00: -------- -------- ------xx xxxxxxxx   ram x

	// 0x01: x------- -------- -------- --------   inverse transparency? (used by kbm)
	// 0x01: -x------ -------- -------- --------   object transparency enable?
	// 0x01: --x----- -------- -------- --------   blend mode?
	// 0x01: ---x---- -------- -------- --------   blend mode?
	// 0x01: ----x--- -------- -------- --------   object y flip
	// 0x01: -----x-- -------- -------- --------   object x flip
	// 0x01: -------- xxxxxxxx xxxxxx-- --------   object y
	// 0x01: -------- -------- ------xx xxxxxxxx   object x

	// 0x02: xxxxx--- -------- -------- --------   alpha1_2
	// 0x02: -----xxx xx------ -------- --------   alpha1_1/source buffer blend factor?
	// 0x02: -------- --xxxxxx xxxxxx-- --------   object x scale
	// 0x02: -------- -------- ------xx xxxxxxxx   object width

	// 0x03: xxxxx--- -------- -------- --------   alpha2_2
	// 0x03: -----xxx xx------ -------- --------   alpha2_1/output buffer blend factor?
	// 0x03: -------- --xxxxxx xxxxxx-- --------   object y scale
	// 0x03: -------- -------- ------xx xxxxxxxx   object height

	uint32_t address_x = BIT(cmd[0], 0, 10);
	uint32_t address_y = BIT(cmd[0], 10, 14);
	bool relative_coords = BIT(cmd[0], 28) == 1;

	int x = BIT(cmd[1], 0, 10);
	int y = BIT(cmd[1], 10, 14);
	bool xflip = BIT(cmd[1], 26) == 1;
	bool yflip = BIT(cmd[1], 27) == 1;
	bool flag_bit28 = BIT(cmd[1], 28) == 1; // 28 and 29 seem to have something to do with blending
	bool flag_bit29 = BIT(cmd[1], 29) == 1;
	bool flag_bit30 = BIT(cmd[1], 30) == 1; // 30 and 31 seem to have something to do with transparency
	bool flag_bit31 = BIT(cmd[1], 31) == 1;
	bool trans_enable = flag_bit31 || flag_bit30;
	uint16_t trans_value = flag_bit31 ? 0x0000 : 0x8000;

	int width = BIT(cmd[2], 0, 9) + 1;
	int xscale = util::sext(BIT(cmd[2], 10, 12), 12);
	int alpha1_1 = BIT(cmd[2], 22, 5);
	int alpha1_2 = BIT(cmd[2], 27, 5);

	int height = BIT(cmd[3], 0, 10) + 1;
	int yscale = util::sext(BIT(cmd[3], 10, 12), 12);
	int alpha2_1 = BIT(cmd[3], 22, 5);
	int alpha2_2 = BIT(cmd[3], 27, 5);

	// This is a big guess and is probably wrong
	// What's the actual difference between alpha1_1 and alpha1_2?
	int alpha1 = 16;
	if (flag_bit30)
		alpha1 = alpha1_2;
	else if (flag_bit31)
		alpha1 = alpha1_1;

	int alpha2 = 16;
	if (flag_bit28)
		alpha2 = alpha2_2;
	else if (flag_bit29)
		alpha2 = alpha2_1;

	if (xscale == 0 || yscale == 0)
		return;

	if (xflip && ((4 - ((width - 1) % 4)) <= (address_x % 4)))
	{
		// Based on logic from pop'n music 8 @ 0x800b30d0
		address_x -= 4;
	}

	if (yflip)
	{
		// Based on logic from pop'n music 8 @ 0x800b3140
		y -= (((height * 64) - 1) / yscale) - (((height - 1) * 64) / yscale);
	}

	if (relative_coords)
	{
		x += m_fb_origin_x;
		y += m_fb_origin_y;
	}

	uint32_t address = (address_y << 10) | address_x;
	int orig_height = height;

	LOGDRAW("%s Draw Object %08X (%d, %d), x %d, y %d, w %d, h %d, sx: %f, sy: %f, alpha [%d %d %d %d | %d %d %d %d] [%08X %08X %08X %08X]\n", basetag(), address, address_x, address_y, x, y, width, height, 64.0f / (float)xscale, 64.0f / (float)yscale, alpha1_1, alpha1_2, alpha2_1, alpha2_2, flag_bit31, flag_bit30, flag_bit29, flag_bit28, cmd[0], cmd[1], cmd[2], cmd[3]);

	width = (((width * 65536) / xscale) * 64) / 65536;
	height = (((height * 65536) / yscale) * 64) / 65536;

	if (height <= 0 || width <= 0)
		return;

	int v = 0;
	int xinc = xflip ? -1 : 1;
	uint16_t *vram16 = (uint16_t*)m_vram.get();
	for (int j = 0; j < height; j++)
	{
		int index;
		uint32_t fbaddr = ((j + y) * FB_PITCH) + x;

		if (yflip)
			index = address + ((orig_height - 1 - (v >> 6)) * FB_PITCH);
		else
			index = address + ((v >> 6) * FB_PITCH);

		if (xflip)
			fbaddr += width - 1;

		int u = 0;
		for (int i = 0; i < width; i++)
		{
			uint16_t pix = vram16[((index + (u >> 6)) ^ NATIVE_ENDIAN_VALUE_LE_BE(1,0))];

			bool draw = !trans_enable || (trans_enable && ((pix & 0x8000) == trans_value));

			if (draw)
			{
				pix = (std::clamp<uint8_t>((BIT(pix, 10, 5) * alpha1) >> 4, 0, 0x1f) << 10)
					| (std::clamp<uint8_t>((BIT(pix, 5, 5) * alpha1) >> 4, 0, 0x1f) << 5)
					| std::clamp<uint8_t>((BIT(pix, 0, 5) * alpha1) >> 4, 0, 0x1f)
					| (pix & 0x8000);

				if (flag_bit28 || flag_bit29)
				{
					uint16_t srcpix = vram16[fbaddr ^ NATIVE_ENDIAN_VALUE_LE_BE(1,0)];

					srcpix = (std::clamp<uint8_t>((BIT(srcpix, 10, 5) * alpha2) >> 4, 0, 0x1f) << 10)
						| (std::clamp<uint8_t>((BIT(srcpix, 5, 5) * alpha2) >> 4, 0, 0x1f) << 5)
						| std::clamp<uint8_t>((BIT(srcpix, 0, 5) * alpha2) >> 4, 0, 0x1f)
						| (srcpix & 0x8000);

					// Blend the two colors
					pix = (std::clamp<uint8_t>(BIT(pix, 10, 5) + BIT(srcpix, 10, 5), 0, 0x1f) << 10)
						| (std::clamp<uint8_t>(BIT(pix, 5, 5) + BIT(srcpix, 5, 5), 0, 0x1f) << 5)
						| std::clamp<uint8_t>(BIT(pix, 0, 5) + BIT(srcpix, 0, 5), 0, 0x1f)
						| (srcpix & 0x8000); // whose alpha do we take?
				}

				vram16[fbaddr ^ NATIVE_ENDIAN_VALUE_LE_BE(1,0)] = pix;
			}

			fbaddr += xinc;
			u += xscale;
		}

		v += yscale;
	}
}

void k057714_device::fill_rect(uint32_t *cmd)
{
	// 0x00: xxx----- -------- -------- --------   command (4)
	// 0x00: ---x---- -------- -------- --------   0: absolute coordinates
	//                                             1: relative coordinates from framebuffer origin
	// 0x00: ----xxxx xxx----- -------- --------   unk1
	// 0x00: -------- ---x---- -------- --------   unk2 bit 0
	// 0x00: -------- ----xxxx xxxxxx-- --------   height
	// 0x00: -------- -------- ------xx xxxxxxxx   width

	// 0x01: x------- -------- -------- --------   ?
	// 0x01: -x------ -------- -------- --------   ?
	// 0x01: --xxxxxx -------- -------- --------   unk2 bits 1-6
	// 0x01: -------- xxxxxxxx xxxxxx-- --------   y
	// 0x01: -------- -------- ------xx xxxxxxxx   x

	// 0x02: xxxxxxxx xxxxxxxx -------- --------   fill pattern pixel 0
	// 0x02: -------- -------- xxxxxxxx xxxxxxxx   fill pattern pixel 1

	// 0x03: xxxxxxxx xxxxxxxx -------- --------   fill pattern pixel 2
	// 0x03: -------- -------- xxxxxxxx xxxxxxxx   fill pattern pixel 3

	const int width = BIT(cmd[0], 0, 10) + 1;
	const int height = BIT(cmd[0], 10, 10) + 1;
	const bool relative_coords = BIT(cmd[0], 28) == 1;

	int x = BIT(cmd[1], 0, 10);
	int y = BIT(cmd[1], 10, 14);

	// Depending on the game these seem to default to a value of 0x20 or 0x40, but enchlamp sets unk2 to 1 at one point
	const int unk1 = BIT(cmd[0], 21, 7);
	const int unk2 = (BIT(cmd[1], 24, 6) << 1) | BIT(cmd[0], 20); // bit 0 is stashed in cmd[0]

	if (relative_coords)
	{
		x += m_fb_origin_x;
		y += m_fb_origin_y;
	}

	uint16_t color[4];
	color[0] = (cmd[2] >> 16);
	color[1] = (cmd[2] & 0xffff);
	color[2] = (cmd[3] >> 16);
	color[3] = (cmd[3] & 0xffff);

	LOGCMDEXEC("%s Fill Rect x %d, y %d, w %d, h %d, unk1 %02x, unk2 %02x, %08X %08X [%08X %08X %08X %08X]\n", basetag(), x, y, width, height, unk1, unk2, cmd[2], cmd[3], cmd[0], cmd[1], cmd[2], cmd[3]);

	int x1 = x;
	int x2 = x + width;
	int y1 = y;
	int y2 = y + height;

	uint16_t *vram16 = (uint16_t*)m_vram.get();

	for (int j = y1; j < y2; j++)
	{
		uint32_t fbaddr = j * FB_PITCH;
		for (int i = x1; i < x2; i++)
		{
			vram16[(fbaddr + i) ^ NATIVE_ENDIAN_VALUE_LE_BE(1,0)] = color[i & 3];
		}
	}
}

void k057714_device::draw_character(uint32_t *cmd)
{
	// 0x00: xxx----- -------- -------- --------   command (6 or 7) (TODO: What's the difference?)
	// 0x00: ---xxxxx -------- -------- --------   width
	// 0x00: -------- xxxxxxxx xxxxxxxx xxxxxxxx   character data address in vram

	// 0x01: x------- -------- -------- --------   ?
	// 0x01: -x------ -------- -------- --------   ?
	// 0x01: --x----- -------- -------- --------   ?
	// 0x01: ---xxxxx -------- -------- --------   height
	// 0x01: -------- xxxxxxxx xxxxxx-- --------   character y
	// 0x01: -------- -------- ------xx xxxxxxxx   character x

	// 0x02: xxxxxxxx xxxxxxxx -------- --------   color 0
	// 0x02: -------- -------- xxxxxxxx xxxxxxxx   color 1

	// 0x03: xxxxxxxx xxxxxxxx -------- --------   color 2
	// 0x03: -------- -------- xxxxxxxx xxxxxxxx   color 3

	int x = cmd[1] & 0x3ff;
	int y = (cmd[1] >> 10) & 0x3fff;
	uint32_t address = cmd[0] & 0xffffff;
	uint16_t color[4];

	int width = (BIT(cmd[0], 24, 5) + 1) * 8;
	int height = (BIT(cmd[1], 24, 5) + 1) * 8;

	color[0] = cmd[2] >> 16;
	color[1] = cmd[2] & 0xffff;
	color[2] = cmd[3] >> 16;
	color[3] = cmd[3] & 0xffff;

	LOGCMDEXEC("%s Draw Char %08X, x %d, y %d, w %d, h %d [%08X %08X %08X %08X]\n", basetag(), address, x, y, width, height, cmd[0], cmd[1], cmd[2], cmd[3]);

	// Haven't found any cases that hit this yet, but this would probably break if a case is ever found
	if (width > 8)
		fatalerror("character widths greater than 8 are not supported, found %d\n", width);
	if (height > 16)
		fatalerror("character heights greater than 16 are not supported, found %d\n", height);

	uint16_t *vram16 = (uint16_t*)m_vram.get();

	for (int j = 0; j < height; j++)
	{
		uint32_t fbaddr = (y + j) * FB_PITCH;
		uint16_t line = vram16[address ^ NATIVE_ENDIAN_VALUE_LE_BE(1,0)];

		address += 4;

		for (int i = 0; i < 8; i++)
		{
			int p = BIT(line, (7 - i) * 2, 2);
			vram16[(fbaddr + x + i) ^ NATIVE_ENDIAN_VALUE_LE_BE(1,0)] = color[p];
		}
	}
}

void k057714_device::fb_config(uint32_t *cmd)
{
	// 0x00: xxx----- -------- -------- --------   command (3)
	// 0x00: -------- -------- ------xx xxxxxxxx   Unknown, always set to 0 (something relating to x or width)

	// 0x01: -------- -------- --xxxxxx xxxxxxxx   Unknown, always set to 0 (something relating to y or height)

	// 0x02: -------- -------- ------xx xxxxxxxx   Framebuffer Origin X

	// 0x03: -------- -------- --xxxxxx xxxxxxxx   Framebuffer Origin Y

	LOGCMDEXEC("%s FB Config %08X %08X %08X %08X\n", basetag(), cmd[0], cmd[1], cmd[2], cmd[3]);

	m_fb_origin_x = cmd[2] & 0x3ff;
	m_fb_origin_y = cmd[3] & 0x3fff;
}

void k057714_device::execute_display_list(uint32_t addr)
{
	bool end = false;

	int counter = 0;

	LOGCMDEXEC("%s Exec Display List %08X\n", basetag(), addr);

	addr /= 2;
	while (!end && counter < 0x1000 && addr < (VRAM_SIZE / 4))
	{
		uint32_t *cmd = &m_vram[addr];
		addr += 4;

		int command = (cmd[0] >> 29) & 0x7;

		switch (command)
		{
			case 0:     // NOP?
				break;

			case 1:     // Execute display list
				execute_display_list(cmd[0] & 0xffffff);
				break;

			case 2:     // End of display list
				end = true;
				break;

			case 3:     // Framebuffer config
				fb_config(cmd);
				break;

			case 4:     // Fill rectangle
				fill_rect(cmd);
				break;

			case 5:     // Draw object
				draw_object(cmd);
				break;

			case 6:
			case 7:     // Draw 8x8 character (2 bits per pixel)
				draw_character(cmd);
				break;

			default:
				LOGCMDEXEC("GCU Unknown command %08X %08X %08X %08X\n", cmd[0], cmd[1], cmd[2], cmd[3]);
				break;
		}
		counter++;
	};
}

void k057714_device::execute_command(uint32_t* cmd)
{
	int command = (cmd[0] >> 29) & 0x7;

	LOGCMDEXEC("%s Exec Command %08X, %08X, %08X, %08X\n", basetag(), cmd[0], cmd[1], cmd[2], cmd[3]);

	switch (command)
	{
		case 0:     // NOP?
			break;

		case 1:     // Execute display list
			execute_display_list(cmd[0] & 0xffffff);
			break;

		case 2:     // End of display list
			break;

		case 3:     // Framebuffer config
			fb_config(cmd);
			break;

		case 4:     // Fill rectangle
			fill_rect(cmd);
			break;

		case 5:     // Draw object
			draw_object(cmd);
			break;

		case 6:
		case 7:     // Draw 8x8 character (2 bits per pixel)
			draw_character(cmd);
			break;

		default:
			LOGCMDEXEC("GCU Unknown command %08X %08X %08X %08X\n", cmd[0], cmd[1], cmd[2], cmd[3]);
			break;
	}
}
