// license:BSD-3-Clause
// copyright-holders:windyfairy
/***************************************************************************

    Bust a Move 2 - Dance Tengoku Mix subboard HLE

***************************************************************************/
#ifndef MAME_SONY_ZN_BAM2_H
#define MAME_SONY_ZN_BAM2_H

#pragma once

#include "zn.h"
#include "imagedev/harddriv.h"
#include "imagedev/cdromimg.h"
#include "sound/dmadac.h"

class bam2_hle_state : public zn_state
{
public:
	enum {
		CDROM_REGION_JAPAN = 0,
		CDROM_REGION_KOREAN,
	};

	void bam2(machine_config &config);

protected:
	bam2_hle_state(const machine_config &mconfig, device_type type, const char *tag);

	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	virtual bool file_exists(uint32_t fileid) = 0;
	virtual void play_audio(uint32_t fileid) = 0;

	void main_map(address_map &map) ATTR_COLD;

	void mcu_w(offs_t offset, uint16_t data);
	uint16_t mcu_r(offs_t offset, uint16_t mem_mask = ~0);

	void unk_w(offs_t offset, uint16_t data);
	uint16_t unk_r();

	TIMER_CALLBACK_MEMBER(audio_playback_flag);

	virtual TIMER_CALLBACK_MEMBER(audio_playback) = 0;

	required_memory_region m_bankedroms;
	required_memory_bank m_rombank;

	required_device_array<dmadac_sound_device, 2> m_dmadac;
	required_device<ata_interface_device> m_ata;

	bool m_media_is_present;
	bool m_media_is_hdd;

	uint16_t m_mcu_command;
	uint16_t m_mcu_response;

	bool m_audio_playing;
	uint32_t m_fileid;
	uint32_t m_volume;
	uint32_t m_mirror_idx;

	uint32_t m_audio_remaining_samples;
	uint32_t m_audio_read_bytes;
	uint32_t m_audio_filesize;

	emu_timer *m_audio_timer;
	emu_timer *m_audio_timer_flag;
};

class bam2_hle_hdd_state : public bam2_hle_state
{
public:
	bam2_hle_hdd_state(const machine_config &mconfig, device_type type, const char *tag);

	void bam2(machine_config &config);

private:
	typedef struct fat32_file_record
	{
		uint32_t cluster;
		uint32_t length;
	} fat32_file_record;

	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	void parse_filesystem();

	virtual bool file_exists(uint32_t fileid) override;
	virtual void play_audio(uint32_t fileid) override;

	virtual TIMER_CALLBACK_MEMBER(audio_playback) override;

	harddisk_image_device *m_image;

	uint32_t m_sectors_per_cluster;
	uint32_t m_bytes_per_sector;
	uint32_t m_first_data_sector;
	uint32_t m_audio_cluster;

	std::vector<uint32_t> clusters;
	std::map<uint32_t, fat32_file_record> m_file_records;
};

class bam2_hle_cdrom_state : public bam2_hle_state
{
public:
	bam2_hle_cdrom_state(const machine_config &mconfig, device_type type, const char *tag);

	void bam2(machine_config &config);

private:
	typedef struct iso9660_file_record
	{
		uint32_t lba;
		uint32_t length;
	} iso9660_file_record;

	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	void parse_filesystem();

	virtual bool file_exists(uint32_t fileid) override;
	virtual void play_audio(uint32_t fileid) override;

	virtual TIMER_CALLBACK_MEMBER(audio_playback) override;

	cdrom_image_device *m_image;

	uint32_t m_audio_lba;
	uint32_t m_audio_filesize;

	std::map<uint32_t, iso9660_file_record> m_file_records;
};

#endif
