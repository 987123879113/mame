// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 Multi Session Unit
 *
 */
#ifndef MAME_KONAMI_K573MSU_H
#define MAME_KONAMI_K573MSU_H

#pragma once

#include <deque>

#include "cpu/tx3927/tx3927.h"
#include "bus/ata/ataintf.h"
#include "bus/ata/atapicdr.h"
#include "machine/ds2401.h"
#include "machine/ins8250.h"
#include "machine/ram.h"
#include "machine/timekpr.h"
#include "machine/timer.h"
#include "sound/tc9446f.h"

DECLARE_DEVICE_TYPE(KONAMI_573_MULTI_SESSION_UNIT, k573msu_device)

class k573msu_device : public device_t
{
public:
	k573msu_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	template<unsigned N> void mpeg_frame_sync(int state);
	template<unsigned N> void audio_demand(int state);

protected:
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_add_mconfig(machine_config &config) override;

	template<unsigned N> void ata_interrupt(int state);
	template<unsigned N> void serial_interrupt(int state);

	TIMER_DEVICE_CALLBACK_MEMBER(fifo_timer_callback);

	void amap(address_map& map);

	virtual const tiny_rom_entry* device_rom_region() const override;
	virtual ioport_constructor device_input_ports() const override;

private:
	required_device<ds2401_device> digital_id;
	required_device<tx3927_device> m_maincpu;
	required_device_array<pc16552_device, 2> m_duart_com;
	required_device<ata_interface_device> m_ata_cdrom;
	required_device_array<tc9446f_device, 4> m_dsp;

	uint16_t fpga_dsp_read(offs_t offset, uint16_t mem_mask);
	void fpga_dsp_write(offs_t offset, uint16_t data, uint16_t mem_mask);

	uint16_t fpga_read(offs_t offset, uint16_t mem_mask);
	void fpga_write(offs_t offset, uint16_t data, uint16_t mem_mask);

	uint16_t ata_command_r(offs_t offset, uint16_t mem_mask = ~0);
	void ata_command_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	uint16_t ata_control_r(offs_t offset, uint16_t mem_mask = ~0);
	void ata_control_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	template <unsigned N>
	uint16_t duart_read(offs_t offset, uint16_t mem_mask = ~0);
	template <unsigned N>
	void duart_write(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	void update_playback_timer(int idx);

	uint32_t m_dsp_unk_flags[0x100] = {};
	int8_t m_dsp_fifo_read_len[4] = {};
	int8_t m_dsp_fifo_write_len[4] = {};
	uint16_t m_dsp_fifo_status;
	uint16_t m_dsp_dest_flag;
	bool m_dsp_fifo_irq_triggered;

	int m_playback_frame[4];
	attotime m_playback_timers[4];
	attotime m_playback_timers_base[4];

	const int m_dsp_fifo_len = 0x100;
	bool m_dsp_fifo_enabled[4];
	std::deque<uint8_t> m_dsp_fifo_buffer[4];
};

#endif // MAME_KONAMI_K573MSU_H
