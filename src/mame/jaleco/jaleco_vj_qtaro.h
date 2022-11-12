// license:BSD-3-Clause
// copyright-holders:windyfairy

#ifndef MAME_JALECO_JALECO_VJ_QTARO_H
#define MAME_JALECO_JALECO_VJ_QTARO_H

#pragma once

#include "machine/pci.h"

struct plm_video_t;
struct plm_buffer_t;

class jaleco_vj_qtaro_device : public device_t
{
public:
    jaleco_vj_qtaro_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

    void reset_stream();

    void mix_w(offs_t offset, uint16_t data);

    uint8_t reg_r(offs_t offset);
    void reg_w(offs_t offset, uint8_t data);

    uint8_t reg2_r(offs_t offset);

    uint32_t reg3_r(offs_t offset);
    void reg3_w(offs_t offset, uint32_t data);

    void write(uint8_t data);

    void set_video_decode_enabled(bool is_enabled);

    void update_frame(double elapsed_time);
    void render_video_frame(bitmap_rgb32& base);

protected:
    virtual void device_start() override;
    virtual void device_reset() override;

private:
    bitmap_rgb32 m_video_frame;

    uint8_t *m_raw_video;

    uint8_t m_int;

    plm_video_t *m_plm_video;
    plm_buffer_t *m_plm_buffer;
    double m_plm_cur_time;
    bool m_found_first_frame;

    uint32_t m_mix_level;
    bool m_video_decode_enabled;

    uint32_t m_frame_width, m_frame_height;
};

DECLARE_DEVICE_TYPE(JALECO_VJ_QTARO, jaleco_vj_qtaro_device)

/////////////////////////////////////////////

class jaleco_vj_king_qtaro_device : public pci_device
{
public:
    jaleco_vj_king_qtaro_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);
    jaleco_vj_king_qtaro_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

    template <typename T> void set_bus_master_space(T &&bmtag, int bmspace) { m_dma_space.set_tag(std::forward<T>(bmtag), bmspace); }
    template <bool R> void set_bus_master_space(const address_space_finder<R> &finder) { m_dma_space.set_tag(finder); }

    void video_control_w(offs_t offset, uint16_t data);

protected:
    virtual void device_start() override;
    virtual void device_reset() override;
    virtual void device_add_mconfig(machine_config &config) override;

private:
    void map(address_map &map);

    uint32_t qtaro_fpga_firmware_status_r(offs_t offset);
    void qtaro_fpga_firmware_status_w(offs_t offset, uint32_t data);

    uint32_t qtaro_fpga_firmware_r(offs_t offset);
    void qtaro_fpga_firmware_w(offs_t offset, uint32_t data);

    uint32_t king_qtaro_fpga_firmware_status_r(offs_t offset);
    void king_qtaro_fpga_firmware_status_w(offs_t offset, uint32_t data);

    uint32_t king_qtaro_fpga_firmware_r(offs_t offset);
    void king_qtaro_fpga_firmware_w(offs_t offset, uint32_t data);

    uint8_t event_io_mask_r(offs_t offset);
    void event_io_mask_w(offs_t offset, uint8_t data);

    uint8_t event_unk_r(offs_t offset);
    void event_unk_w(offs_t offset, uint8_t data);

    uint8_t event_io_r(offs_t offset);
    void event_io_w(offs_t offset, uint8_t data);

    uint32_t event_r(offs_t offset);
    void event_w(offs_t offset, uint32_t data);

    uint32_t event_mask_r(offs_t offset);
    void event_mask_w(offs_t offset, uint32_t data);

    uint32_t int_r(offs_t offset);
    void int_w(offs_t offset, uint32_t data);

    uint32_t int_fpga_r(offs_t offset);
    void int_fpga_w(offs_t offset, uint32_t data);

    template <int device_id> void qtaro_dma_requested_w(offs_t offset, uint32_t data);

    template <int device_id> void qtaro_dma_descriptor_phys_addr_w(offs_t offset, uint32_t data);

    template <int device_id> uint32_t qtaro_dma_running_r(offs_t offset);
    template <int device_id> void qtaro_dma_running_w(offs_t offset, uint32_t data);

    TIMER_CALLBACK_MEMBER(video_dma_callback);

    required_address_space m_dma_space;
    required_device_array<jaleco_vj_qtaro_device, 3> m_qtaro;

    emu_timer* m_dma_timer;

    uint32_t m_int;
    uint32_t m_int_fpga;

    uint32_t m_event, m_event_mask;
    uint8_t m_event_io[5], m_event_io_mask[5];
    uint8_t m_event_unk[5], m_event_unk_mask[5];

    bool m_dma_running[3];
    uint32_t m_dma_descriptor_addr[3];
    uint32_t m_dma_descriptor_length[3];
    uint32_t m_last_video_control;

    attotime m_video_last_update;
};

DECLARE_DEVICE_TYPE(JALECO_VJ_KING_QTARO, jaleco_vj_king_qtaro_device)

#endif  // MAME_JALECO_JALECO_VJ_QTARO_H
