// license:BSD-3-Clause
// copyright-holders:windyfairy

/*
King Qtaro PCI card and Qtaro subboard for VJ

Connects to the VJ-98348 subboard PCB via 40-pin ribbon cable

Main board ("King Qtaro"):
No markings
----------------------------------------------
|                                            |
|                                            |
|                                            |
|                             FLEX           |
|       CN3                              CN2 |
|                                            |
|                                            |
|                   LS6201                   |
|                                            |
|    |-------|           |-|    |------------|
------       ------------- ------

LS6201 - LSI LS6201 027 9850KX001 PCI Local Bus Interface
FLEX - Altera Flex EPF10K10QC208-4 DAB239813
CN2, CN3 - 68-pin connectors

Information about the LS6201:
https://web.archive.org/web/20070912033617/http://www.lsisys.co.jp/prod/ls6201/ls6201.htm
https://web.archive.org/web/20001015203836/http://www.lsisys.co.jp:80/prod/LS6201.pdf

The King Qtaro board appears to be a custom spec ordered from LSI Systems and shares some
layout similarities to the LS6201 evaluation card offered by LSI Systems.


JALECO VJ-98347
MADE IN JAPAN
EB-00-20125-0
(Front)
-----------------------------------
|    CN2        CN4        CN6    |
|                                 |
|                                 |
|                                 |
|                                 |
|                                 |
|    CN1        CN3        CN5    |
-----------------------------------

CN1/2/3/4/5/6 - 80-pin connectors

(Back)
-----------------------------------
|               CN9               |
|                                 |
|               U4                |
|  CN7          U1           CN8  |
|               U2                |
|               U3                |
|                                 |
-----------------------------------

U1 - ?
U2, U3 - (Unpopulated)
U4 - HM87AV LM3940IS
CN7, CN8 - 68-pin connectors. Connects to CN3, CN2 on main board


JALECO VJ-98341 ("Qtaro")
MADE IN JAPAN
EB-00-20124-0
----------------------------------------
|                                      |
|   |----------|                       |
|   |          |   D4516161            |
|   |   FLEX   |             U2  CN3   |
|   |          |                   CN4 |
|   |----------|             U1        |
|                                      |
|                                      |
----------------------------------------
FLEX     - Altera FLEX EPF10K30AQC240-3 DBA439849
D4516161 - NEC uPD4516161AG5-A80 512K x 16-bit x 2-banks (16MBit) SDRAM (SSOP50)
U1, U2   - LVX244

Communication with ADV7176AKS using one of these cables but it's hard to tell which (maybe CN3 based on traces?).
CN3 - 40 pin connector (video output, connects to VJ-98342, sprites)
CN4 - 40 pin connector (video output, connects to VJ-98342, decoded video stream)

Hardware testing confirms that the Qtaro board is responsible for mixing the sprites from the subboard
with the movies from the PC side.
On real hardware, when the CN3 ribbon cables for two monitors going into the subboard are swapped
but CN4 is left in its proper ordering, the sprites will appear based on the placement of the ribbon
cable on the subboard. The movies are still in the correct ordering.
*/

#include "emu.h"
#include "jaleco_vj_qtaro.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg/pl_mpeg.h"

#define DMA_TIMER_PERIOD attotime::from_usec(250)

#define LOG_GENERAL            (1 << 0)
#define LOG_VIDEO              (1 << 1)
#define LOG_DMA                (1 << 2)
#define LOG_VERBOSE_VIDEO      (1 << 3)
#define LOG_VERBOSE_DMA        (1 << 4)
#define LOG_VERBOSE_VIDEO_DATA (1 << 5)
// #define VERBOSE        (LOG_GENERAL | LOG_VIDEO | LOG_DMA | LOG_VERBOSE_VIDEO | LOG_VERBOSE_DMA)
// #define LOG_OUTPUT_STREAM std::cout

#include "logmacro.h"


jaleco_vj_qtaro_device::jaleco_vj_qtaro_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
    : device_t(mconfig, JALECO_VJ_QTARO, tag, owner, clock)
{
}

void jaleco_vj_qtaro_device::device_start()
{
    save_item(NAME(m_int));
    save_item(NAME(m_frame_width));
    save_item(NAME(m_frame_height));
    save_item(NAME(m_mix_level));
    save_item(NAME(m_video_decode_enabled));
    save_item(NAME(m_found_first_frame));
    save_item(NAME(m_plm_cur_time));
}

void jaleco_vj_qtaro_device::device_reset()
{
    m_int = 0;

    m_plm_video = nullptr;
    m_plm_buffer = nullptr;

    m_frame_width = 352;
    m_frame_height = 240;

    m_raw_video = nullptr;
    m_mix_level = 0;
    m_video_decode_enabled = false;

    reset_stream();
}

void jaleco_vj_qtaro_device::reset_stream()
{
    m_plm_cur_time = 0;
    m_found_first_frame = false;

    if (m_plm_video != nullptr) {
        plm_video_destroy(m_plm_video);
    }

    if (m_plm_buffer != nullptr) {
        plm_buffer_destroy(m_plm_buffer);
    }

    m_plm_buffer = plm_buffer_create_for_appending(0x200000);
    m_plm_video = plm_video_create_with_buffer(m_plm_buffer, false);
}

void jaleco_vj_qtaro_device::mix_w(offs_t offset, uint16_t data)
{
    if (data != m_mix_level) {
        LOGMASKED(LOG_VIDEO, "[%s] mix_w %04x\n", tag(), data);
    }

    m_mix_level = data;
}

uint8_t jaleco_vj_qtaro_device::reg_r(offs_t offset)
{
    return m_int;
}

void jaleco_vj_qtaro_device::reg_w(offs_t offset, uint8_t data)
{
    // Bit 7 is set when starting DMA for an entirely new video, then unset when video is ended
    if (BIT(data, 7) && !BIT(m_int, 7))  {
        LOGMASKED(LOG_VIDEO, "[%s] DMA transfer thread started %02x\n", tag(), data);
        reset_stream();
    } else if (!BIT(data, 7) && BIT(m_int, 7))  {
        LOGMASKED(LOG_VIDEO, "[%s] DMA transfer thread ended %02x\n", tag(), data);
        plm_buffer_signal_end(m_plm_buffer);
    }

    m_int = data;
}

uint8_t jaleco_vj_qtaro_device::reg2_r(offs_t offset)
{
    return 0;
}

uint32_t jaleco_vj_qtaro_device::reg3_r(offs_t offset)
{
    // 0x20 is some kind of default state. Relates to DMAs or video playback.
    // If this value is 0x40 then the code sets it back to 0x20 during the WriteStream cleanup function.
    return 0x20;
}

void jaleco_vj_qtaro_device::reg3_w(offs_t offset, uint32_t data)
{
}

void jaleco_vj_qtaro_device::write(uint8_t data)
{
    LOGMASKED(LOG_VERBOSE_VIDEO_DATA, "[%s] video data write %02x\n", tag(), data);
    plm_buffer_write(m_plm_buffer, &data, 1);
}

void jaleco_vj_qtaro_device::set_video_decode_enabled(bool is_enabled)
{
    if (is_enabled && !m_video_decode_enabled)
        LOGMASKED(LOG_VIDEO, "[%s] video decoding enabled\n", tag());
    else if (!is_enabled && m_video_decode_enabled)
        LOGMASKED(LOG_VIDEO, "[%s] video decoding disabled\n", tag());

    m_video_decode_enabled = is_enabled;
}

void jaleco_vj_qtaro_device::update_frame(double elapsed_time)
{
    if (m_video_decode_enabled && m_plm_video != nullptr) {
        m_plm_cur_time += elapsed_time;

        while (plm_video_get_time(m_plm_video) <= m_plm_cur_time) {
            plm_frame_t *frame = plm_video_decode(m_plm_video);

            LOGMASKED(LOG_VERBOSE_VIDEO, "[%s] video frame generated %d %d %d\n", tag(), plm_video_get_time(m_plm_video), m_plm_cur_time, frame != nullptr);

            if (frame == nullptr)
                break;

            if (!m_found_first_frame) {
                if (m_raw_video != nullptr)
                    free(m_raw_video);

                m_raw_video = (uint8_t*)malloc(plm_video_get_width(m_plm_video) * plm_video_get_height(m_plm_video) * 4);
                m_found_first_frame = true;
            }

            plm_frame_to_bgra(frame, m_raw_video, frame->width * 4);
            m_frame_width = frame->width;
            m_frame_height = frame->height;
        }
    }
}

void jaleco_vj_qtaro_device::render_video_frame(bitmap_rgb32& base)
{
    if (!m_raw_video || !m_video_decode_enabled || m_mix_level >= 15)
        return;

    assert(base.width() == m_frame_width);
    assert(base.height() == m_frame_height);

    if (m_mix_level == 0) {
        // Full movie frame
        copybitmap(
            base,
            bitmap_rgb32(
                (uint32_t*)m_raw_video,
                m_frame_width,
                m_frame_height,
                m_frame_width
            ),
            0, 0, 0, 0,
            rectangle(0, base.width(), 0, base.height())
        );
        return;
    }

    // TODO: Stepping stage sets the mix level to 15 even when it's supposed to be playing videos
    // When a song ends and the video fades the black, the level goes from 0 to 15
    const double overlay_blend = m_mix_level / 15.0;
    const double movie_blend = 1.0 - overlay_blend;

    for (int y = 0; y < m_frame_height; y++) {
        for (int x = 0; x < m_frame_width; x++) {
            int offs = (x + (y * m_frame_width)) * 4;
            auto p = &base.pix(y, x);
            uint8_t r = std::min(255.0, m_raw_video[offs] * movie_blend + BIT(*p, 0, 8) * overlay_blend);
            uint8_t g = std::min(255.0, m_raw_video[offs+1] * movie_blend + BIT(*p, 8, 8) * overlay_blend);
            uint8_t b = std::min(255.0, m_raw_video[offs+2] * movie_blend + BIT(*p, 16, 8) * overlay_blend);
            *p = 0xff000000 | (b << 16) | (g << 8) | r;
        }
    }
}

DEFINE_DEVICE_TYPE(JALECO_VJ_QTARO, jaleco_vj_qtaro_device, "jaleco_vj_qtaro", "Jaleco VJ Qtaro Subboard")

/////////////////////////////////////////////

void jaleco_vj_king_qtaro_device::video_control_w(offs_t offset, uint16_t data)
{
    auto cur_update = machine().scheduler().time();
    auto elapsed_time = cur_update - m_video_last_update;
    m_video_last_update = cur_update;

    LOGMASKED(LOG_VIDEO, "video_control_w %04x %lf\n", data, elapsed_time.as_double());

    for (int i = 0; i < 3; i++) {
        // I've only ever seen 0x2a and 0x3f written to this register.
        // Bits 0, 2, 4 change when a video should or shouldn't be displayed
        // Bits 1, 3, 5 are always set?
        bool is_enabled = BIT(data, i * 2, 1) == 0;
        bool was_enabled = BIT(m_last_video_control, i * 2, 1) == 0;

        if (!is_enabled && was_enabled) {
            m_dma_descriptor_addr[i] = 0;
            m_dma_descriptor_length[i] = 0;
            m_dma_running[i] = false;
            m_qtaro[i]->reset_stream();
        }

        m_qtaro[i]->set_video_decode_enabled(is_enabled);

        if (elapsed_time != attotime::never)
            m_qtaro[i]->update_frame(elapsed_time.as_double());
    }

    m_last_video_control = data;
}

uint32_t jaleco_vj_king_qtaro_device::qtaro_fpga_firmware_status_r(offs_t offset)
{
    // Tested when uploading Qtaro firmware
    // 0x100 is set when busy and will keep looping until it's not 0x100
    return 0;
}

void jaleco_vj_king_qtaro_device::qtaro_fpga_firmware_status_w(offs_t offset, uint32_t data)
{
    // Set to 0x80000020 when uploading Qtaro firmware
}

uint32_t jaleco_vj_king_qtaro_device::qtaro_fpga_firmware_r(offs_t offset)
{
    // Should only return 1 when the firmware is finished writing.
    // Returning 1 on the first byte will cause it to stop uploading the firmware,
    // then it'll write 3 0xffs and on the last 0xff if it sees 1 then it thinks it finished
    // uploading the firmware successfully.
    return 1;
}

void jaleco_vj_king_qtaro_device::qtaro_fpga_firmware_w(offs_t offset, uint32_t data)
{
}

uint32_t jaleco_vj_king_qtaro_device::king_qtaro_fpga_firmware_status_r(offs_t offset)
{
    // Tested when uploading King Qtaro firmware
    // 0x100 is set when busy and will keep looping until it's not 0x100
    return 0;
}

void jaleco_vj_king_qtaro_device::king_qtaro_fpga_firmware_status_w(offs_t offset, uint32_t data)
{
    // Set to 0x80000020 when uploading King Qtaro firmware
}

uint32_t jaleco_vj_king_qtaro_device::king_qtaro_fpga_firmware_r(offs_t offset)
{
    // Should only return 1 when the firmware is finished writing.
    // Returning 1 on the first byte will cause it to stop uploading the firmware,
    // then it'll write 3 0xffs and on the last 0xff if it sees 1 then it thinks it finished
    // uploading the firmware successfully.
    return 1;
}

void jaleco_vj_king_qtaro_device::king_qtaro_fpga_firmware_w(offs_t offset, uint32_t data)
{
}

uint8_t jaleco_vj_king_qtaro_device::event_io_mask_r(offs_t offset)
{
    return m_event_io_mask[offset];
}

void jaleco_vj_king_qtaro_device::event_io_mask_w(offs_t offset, uint8_t data)
{
    m_event_io_mask[offset] = data;
}

uint8_t jaleco_vj_king_qtaro_device::event_unk_r(offs_t offset)
{
    return m_event_unk[offset];
}

void jaleco_vj_king_qtaro_device::event_unk_w(offs_t offset, uint8_t data)
{
    m_event_unk[offset] = data;
}

uint8_t jaleco_vj_king_qtaro_device::event_io_r(offs_t offset)
{
    uint8_t r = m_event_io[offset];
    if (offset == 0)
        r |= 0b111; // Some kind of status flag for each Qtaro board? Must be 1 after writing FPGA firmware
    return r;
}

void jaleco_vj_king_qtaro_device::event_io_w(offs_t offset, uint8_t data)
{
    m_event_io[offset] = data;
}

uint32_t jaleco_vj_king_qtaro_device::event_r(offs_t offset)
{
    return m_event;
}

void jaleco_vj_king_qtaro_device::event_w(offs_t offset, uint32_t data)
{
    m_event = data;
}

uint32_t jaleco_vj_king_qtaro_device::event_mask_r(offs_t offset)
{
    return m_event_mask;
}

void jaleco_vj_king_qtaro_device::event_mask_w(offs_t offset, uint32_t data)
{
    // 0xe0e00 is set when it's waiting for some kind of events from the Qtaro boards.
    // If event_r returns any of those bits then it'll trigger an event signal, but the
    // vj.exe program never listens for those events so they don't seem to do anything.
    // The events are created when initializing the video DMA threads and closed when
    // the videos are requested to stop.
    m_event_mask = data;
}

uint32_t jaleco_vj_king_qtaro_device::int_r(offs_t offset)
{
    auto r = m_int & ~0x10;

    if (m_dma_running[0] || m_dma_running[1] || m_dma_running[2]) {
        // The only time 0x10 is referenced is when ending WriteStream for the individual Qtaro devices.
        // All 3 of the WriteStream cleanup functions start by writing 0 to qtaro_dma_requested_w and qtaro_dma_running_w
        // then loop until 0x10 is not set here.
        r |= 0x10;
    }

    return r;
}

void jaleco_vj_king_qtaro_device::int_w(offs_t offset, uint32_t data)
{
    // 0x1000000 is used to trigger an event interrupt in the Qtaro driver.
    // The interrupt will only be accepted and cleared when event_r, event2_r, event_io_r are non-zero.
    // It's set, read, and cleared all in the driver so no need to handle it here.
    m_int = data;
}

uint32_t jaleco_vj_king_qtaro_device::int_fpga_r(offs_t offset)
{
    return m_int_fpga;
}

void jaleco_vj_king_qtaro_device::int_fpga_w(offs_t offset, uint32_t data)
{
    m_int_fpga = data;
}

template <int device_id>
void jaleco_vj_king_qtaro_device::qtaro_dma_requested_w(offs_t offset, uint32_t data)
{
    auto prev = m_dma_running[device_id];

    m_dma_running[device_id] = data == 1;

    LOGMASKED(LOG_DMA, "qtaro_dma_requested_w<%d>: %08x\n", device_id, data);

    if (m_dma_running[device_id] && !prev && m_dma_running[0] && m_dma_running[1] && m_dma_running[2]) {
        LOGMASKED(LOG_VERBOSE_DMA, "DMA transfers started\n");
        m_dma_timer->adjust(DMA_TIMER_PERIOD, 0, DMA_TIMER_PERIOD);
    }
}

template <int device_id>
void jaleco_vj_king_qtaro_device::qtaro_dma_descriptor_phys_addr_w(offs_t offset, uint32_t data)
{
    LOGMASKED(LOG_DMA, "qtaro_dma_descriptor_phys_addr_w<%d>: %08x\n", device_id, data);
    m_dma_descriptor_addr[device_id] = data;
    m_dma_descriptor_length[device_id] = 0;
}

template <int device_id>
uint32_t jaleco_vj_king_qtaro_device::qtaro_dma_running_r(offs_t offset)
{
    // WriteStream won't read more buffer data or send another descriptor while bit 0 is set
    return m_dma_running[device_id];
}

template <int device_id>
void jaleco_vj_king_qtaro_device::qtaro_dma_running_w(offs_t offset, uint32_t data)
{
    LOGMASKED(LOG_DMA, "qtaro_dma_running_w<%d>: %08x\n", device_id, data);

    // Only time this is written to is to write 0 when WriteStream's cleanup function is called
    // m_dma_running[device_id] = data != 0;
}

TIMER_CALLBACK_MEMBER (jaleco_vj_king_qtaro_device::video_dma_callback)
{
    if (!m_dma_running[0] && !m_dma_running[1] && !m_dma_running[2]) {
        m_dma_timer->adjust(attotime::never);
        return;
    }

    for (int device_id = 0; device_id < 3; device_id++) {
        if (!m_dma_running[device_id] || BIT(m_dma_descriptor_addr[device_id], 0))
            continue;

        const uint32_t nextDescriptorPhysAddr = m_dma_space->read_dword(m_dma_descriptor_addr[device_id]);
        const uint32_t dmaLength = m_dma_space->read_dword(m_dma_descriptor_addr[device_id] + 4); // max 32kb
        const uint32_t bufferPhysAddr = m_dma_space->read_dword(m_dma_descriptor_addr[device_id] + 8);
        const uint32_t burstLength = std::min(dmaLength - m_dma_descriptor_length[device_id], 64u);
        // const uint32_t flags = m_dma_space->read_dword(m_dma_descriptor_addr[device_id] + 12); // Bit 24 is set to denote the last entry at the same time as bit 0 of the next descriptor addr is set

        LOGMASKED(LOG_VERBOSE_DMA, "DMA %d copy %08x: %08x bytes\n", device_id, bufferPhysAddr + m_dma_descriptor_length[device_id], burstLength);

        for (int i = 0; i < burstLength; i++) {
            m_qtaro[device_id]->write(m_dma_space->read_byte(bufferPhysAddr + m_dma_descriptor_length[device_id]));
            m_dma_descriptor_length[device_id]++;
        }

        if (m_dma_descriptor_length[device_id] >= dmaLength) {
            LOGMASKED(LOG_VERBOSE_DMA, "DMA %d: %08x -> %08x %08x\n", device_id, m_dma_descriptor_addr[device_id], nextDescriptorPhysAddr, m_dma_descriptor_length[device_id]);

            m_dma_descriptor_addr[device_id] = nextDescriptorPhysAddr;
            m_dma_descriptor_length[device_id] = 0;
        }
    }

    if (BIT(m_dma_descriptor_addr[0], 0) && BIT(m_dma_descriptor_addr[1], 0) && BIT(m_dma_descriptor_addr[2], 0)) {
        LOGMASKED(LOG_VERBOSE_DMA, "DMA transfers finished\n");
        m_dma_running[0] = m_dma_running[1] = m_dma_running[2] = false;
    }
}

void jaleco_vj_king_qtaro_device::map(address_map &map)
{
    map(0x10, 0x10).r(m_qtaro[0], FUNC(jaleco_vj_qtaro_device::reg2_r));
    map(0x18, 0x1b).rw(m_qtaro[0], FUNC(jaleco_vj_qtaro_device::reg3_r), FUNC(jaleco_vj_qtaro_device::reg3_w));
    map(0x20, 0x20).r(m_qtaro[1], FUNC(jaleco_vj_qtaro_device::reg2_r));
    map(0x28, 0x2b).rw(m_qtaro[1], FUNC(jaleco_vj_qtaro_device::reg3_r), FUNC(jaleco_vj_qtaro_device::reg3_w));
    map(0x30, 0x30).r(m_qtaro[2], FUNC(jaleco_vj_qtaro_device::reg2_r));
    map(0x38, 0x3b).rw(m_qtaro[2], FUNC(jaleco_vj_qtaro_device::reg3_r), FUNC(jaleco_vj_qtaro_device::reg3_w));

    map(0x50, 0x53).w(FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_requested_w<0>));
    map(0x54, 0x57).w(FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_descriptor_phys_addr_w<0>));
    map(0x58, 0x5b).rw(FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_running_r<0>), FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_running_w<0>));
    map(0x60, 0x63).w(FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_requested_w<1>));
    map(0x64, 0x67).w(FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_descriptor_phys_addr_w<1>));
    map(0x68, 0x6b).rw(FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_running_r<1>), FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_running_w<1>));
    map(0x70, 0x73).w(FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_requested_w<2>));
    map(0x74, 0x77).w(FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_descriptor_phys_addr_w<2>));
    map(0x78, 0x7b).rw(FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_running_r<2>), FUNC(jaleco_vj_king_qtaro_device::qtaro_dma_running_w<2>));

    map(0x80, 0x83).rw(FUNC(jaleco_vj_king_qtaro_device::qtaro_fpga_firmware_status_r), FUNC(jaleco_vj_king_qtaro_device::qtaro_fpga_firmware_status_w));
    map(0x84, 0x87).rw(FUNC(jaleco_vj_king_qtaro_device::qtaro_fpga_firmware_r), FUNC(jaleco_vj_king_qtaro_device::qtaro_fpga_firmware_w));
    map(0x88, 0x8b).rw(FUNC(jaleco_vj_king_qtaro_device::king_qtaro_fpga_firmware_status_r), FUNC(jaleco_vj_king_qtaro_device::king_qtaro_fpga_firmware_status_w));
    map(0x8c, 0x8f).rw(FUNC(jaleco_vj_king_qtaro_device::king_qtaro_fpga_firmware_r), FUNC(jaleco_vj_king_qtaro_device::king_qtaro_fpga_firmware_w));

    map(0x90, 0x94).rw(FUNC(jaleco_vj_king_qtaro_device::event_io_r), FUNC(jaleco_vj_king_qtaro_device::event_io_w));
    map(0x98, 0x9c).rw(FUNC(jaleco_vj_king_qtaro_device::event_unk_r), FUNC(jaleco_vj_king_qtaro_device::event_unk_w));
    map(0xa0, 0xa4).rw(FUNC(jaleco_vj_king_qtaro_device::event_io_mask_r), FUNC(jaleco_vj_king_qtaro_device::event_io_mask_w));
    map(0xa8, 0xab).rw(FUNC(jaleco_vj_king_qtaro_device::event_mask_r), FUNC(jaleco_vj_king_qtaro_device::event_mask_w));
    map(0xac, 0xaf).rw(FUNC(jaleco_vj_king_qtaro_device::event_r), FUNC(jaleco_vj_king_qtaro_device::event_w));

    map(0xb1, 0xb1).rw(m_qtaro[0], FUNC(jaleco_vj_qtaro_device::reg_r), FUNC(jaleco_vj_qtaro_device::reg_w));
    map(0xb2, 0xb2).rw(m_qtaro[1], FUNC(jaleco_vj_qtaro_device::reg_r), FUNC(jaleco_vj_qtaro_device::reg_w));
    map(0xb3, 0xb3).rw(m_qtaro[2], FUNC(jaleco_vj_qtaro_device::reg_r), FUNC(jaleco_vj_qtaro_device::reg_w));
    map(0xb4, 0xb7).rw(FUNC(jaleco_vj_king_qtaro_device::int_r), FUNC(jaleco_vj_king_qtaro_device::int_w));
    map(0xb8, 0xbb).rw(FUNC(jaleco_vj_king_qtaro_device::int_fpga_r), FUNC(jaleco_vj_king_qtaro_device::int_fpga_w));
}

jaleco_vj_king_qtaro_device::jaleco_vj_king_qtaro_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
    : jaleco_vj_king_qtaro_device(mconfig, JALECO_VJ_KING_QTARO, tag, owner, clock)
{
}

jaleco_vj_king_qtaro_device::jaleco_vj_king_qtaro_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
    : pci_device(mconfig, type, tag, owner, clock),
    m_dma_space(*this, finder_base::DUMMY_TAG, -1, 32),
    m_qtaro(*this, "qtaro%u", 1)
{
}

void jaleco_vj_king_qtaro_device::device_start()
{
    pci_device::device_start();
    set_ids(0x11ca0007, 0x01, 0x068000, 0x00000000);

    intr_line = 10; // No idea what this should be, but a valid IRQ is required to work

    add_map(256, M_MEM, FUNC(jaleco_vj_king_qtaro_device::map));

    m_dma_timer = timer_alloc(FUNC(jaleco_vj_king_qtaro_device::video_dma_callback), this);
    m_dma_timer->adjust(attotime::never);

    save_item(NAME(m_int));
    save_item(NAME(m_int_fpga));
    save_item(NAME(m_event));
    save_item(NAME(m_event_mask));
    save_item(NAME(m_event_io));
    save_item(NAME(m_event_io_mask));
    save_item(NAME(m_event_unk));
    save_item(NAME(m_event_unk_mask));
    save_item(NAME(m_dma_running));
    save_item(NAME(m_dma_descriptor_addr));
    save_item(NAME(m_dma_descriptor_length));
    save_item(NAME(m_last_video_control));
}

void jaleco_vj_king_qtaro_device::device_reset()
{
    m_int = 0;
    m_int_fpga = 0;

    m_event = m_event_mask = 0;
    std::fill(std::begin(m_event_io), std::end(m_event_io), 0);
    std::fill(std::begin(m_event_io_mask), std::end(m_event_io_mask), 0);
    std::fill(std::begin(m_event_unk), std::end(m_event_unk), 0);
    std::fill(std::begin(m_event_unk_mask), std::end(m_event_unk_mask), 0);

    m_last_video_control = 0;
    std::fill(std::begin(m_dma_running), std::end(m_dma_running), false);
    std::fill(std::begin(m_dma_descriptor_addr), std::end(m_dma_descriptor_addr), 0);
    std::fill(std::begin(m_dma_descriptor_length), std::end(m_dma_descriptor_length), 0);

    m_video_last_update = attotime::never;
}

void jaleco_vj_king_qtaro_device::device_add_mconfig(machine_config &config)
{
    JALECO_VJ_QTARO(config, m_qtaro[0], 0);
    JALECO_VJ_QTARO(config, m_qtaro[1], 0);
    JALECO_VJ_QTARO(config, m_qtaro[2], 0);
}

DEFINE_DEVICE_TYPE(JALECO_VJ_KING_QTARO, jaleco_vj_king_qtaro_device, "jaleco_vj_king_qtaro", "Jaleco VJ King Qtaro PCI Device")
