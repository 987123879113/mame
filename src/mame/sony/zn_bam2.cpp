// license:BSD-3-Clause
// copyright-holders:windyfairy
/***************************************************************************

    Bust a Move 2 - Dance Tengoku Mix subboard HLE

    NOTE: Hold the service button during boot to change HDD mirror setting.
    The mirror setting changes the file read on the HDD.
    Make sure it's set to mirror 1 for this to work correctly.

***************************************************************************/

#include "emu.h"
#include "bus/ata/ataintf.h"
#include "bus/ata/hdd.h"
#include "util/multibyte.h"

#include "zn_bam2.h"

/*

Bust A Move 2

Runs on ZN1 hardware
Lower PCB is common ZN1 with COH-1002E bios and ET01 sec chip
Top PCB is unique for this game....

MTR990601-(A)
|----------------------------------------------|
|  ALTERA-MAX   CN5  H8/3644       IDE-40      |
|  EPM7128STC100                               |
|                                              |
|            MTR-BAM-A01.U23   MTR-BAM-A06.U28 |
|   FLASH.U19                                  |
|            MTR-BAM-A02.U24   MTR-BAM-A07.U29 |
|   FLASH.U20                                  |
|            MTR-BAM-A03.U25   MTR-BAM-A08.U30 |
|  *FLASH.U21                                  |
|            MTR-BAM-A04.U26   MTR-BAM-A09.U31 |
|  *FLASH.U22                                  |
|SEC         MTR-BAM-A05.U27   MTR-BAM-A10.U32 |
|    CN3       4560   4560       TC9293        |
|----------------------------------------------|
Notes:
       * - Not populated
   FLASH - MX29F1610 SOP44 flashROMs
MTR-BAM* - DIP42 32MBit maskROMs
  TC9293 - Toshiba TC9293 Modulation System DAC with Analog Filter
    4560 - JRC 4560 Op Amp
     SEC - CAT702 security IC
 CN3/CN5 - Connectors for ? (controls?)
  IDE-40 - 40 Pin flat cable connector for IDE HDD
           HDD is 3.5" Quantum Fireball CR 4.3AT

*/

constexpr uint32_t DMADAC_MAX_SAMPLE_COUNT = 32768;

bam2_hle_state::bam2_hle_state(const machine_config &mconfig, device_type type, const char *tag) :
	zn_state(mconfig, type, tag),
	m_bankedroms(*this, "bankedroms"),
	m_rombank(*this, "rombank"),
	m_dmadac(*this, "dac%u", 0U),
	m_ata(*this, "ata")
{
}

void bam2_hle_state::bam2(machine_config &config)
{
	zn_2mb_vram(config);
	gameboard_cat702(config);

	m_maincpu->set_addrmap(AS_PROGRAM, &bam2_hle_state::main_map);

	DMADAC(config, m_dmadac[0]).add_route(ALL_OUTPUTS, "lspeaker", 1.0);
	DMADAC(config, m_dmadac[1]).add_route(ALL_OUTPUTS, "rspeaker", 1.0);
}

void bam2_hle_state::machine_start()
{
	zn_state::machine_start();

	save_item(NAME(m_media_is_present));

	save_item(NAME(m_mcu_command));
	save_item(NAME(m_mcu_response));

	save_item(NAME(m_audio_playing));
	save_item(NAME(m_fileid));
	save_item(NAME(m_volume));
	save_item(NAME(m_mirror_idx));

	save_item(NAME(m_audio_remaining_samples));
	save_item(NAME(m_audio_read_bytes));
	save_item(NAME(m_audio_filesize));

	m_rombank->configure_entries(0, 16, m_bankedroms->base(), 0x400000); /* banked game ROM */

	m_audio_timer = timer_alloc(FUNC(bam2_hle_state::audio_playback), this);
}

void bam2_hle_state::machine_reset()
{
	m_media_is_present = false;
	m_media_is_hdd = false;

	m_mcu_command = 0;
	m_mcu_response = 0;

	m_audio_playing = false;
	m_fileid = 0xffffffff;
	m_volume = 0;
	m_mirror_idx = 0;

	m_audio_remaining_samples = 0;
	m_audio_read_bytes = 0;
	m_audio_filesize = 0;

	m_rombank->set_entry(1);

	for (int i = 0; i < std::size(m_dmadac); i++)
	{
		m_dmadac[i]->enable(0);
		m_dmadac[i]->set_frequency(44100);
		m_dmadac[i]->set_volume(m_volume);
	}
}

void bam2_hle_state::main_map(address_map &map)
{
	zn_rom_base_map(map);

	map(0x1f000000, 0x1f3fffff).rom().region("bankedroms", 0);
	map(0x1f400000, 0x1f7fffff).bankr("rombank");
	map(0x1fa20000, 0x1fa20001).rw(FUNC(bam2_hle_state::unk_r), FUNC(bam2_hle_state::unk_w));
	map(0x1fb00000, 0x1fb00007).rw(FUNC(bam2_hle_state::mcu_r), FUNC(bam2_hle_state::mcu_w));
}

void bam2_hle_state::mcu_w(offs_t offset, uint16_t data)
{
	switch(offset)
	{
	case 0:
		m_rombank->set_entry(data & 0xf);
		break;

	case 1:
		if (data & 0x80)
		{
			m_mcu_command = data;
		}
		else
		{
			m_mcu_response = 0;

			switch (m_mcu_command)
			{
				case 0x81:
					// load file/check if file exists
					m_fileid = data;

					if (m_media_is_hdd)
					{
						// The HDD is weird. It has the BGM 4 times in each BGM file.
						// The mirroring function can be changed by holding service while the machine is booting.
						// The game code adjusts the requested file ID as: file ID += mirror * 39
						m_mirror_idx = data / 39;
						m_fileid = data % 39;
					}

					m_mcu_response = !file_exists(data);

					break;

				case 0x82:
					// play audio
					m_mcu_response = 4; // loops until command returns 4
					play_audio(m_fileid);
					break;

				case 0x83:
					// stop audio
					m_audio_playing = false;
					m_audio_timer->adjust(attotime::never);

					for (int i = 0; i < std::size(m_dmadac); i++)
						m_dmadac[i]->enable(0);
					break;

				case 0x84:
					// drive status? must be any value besides 2
					m_mcu_response = !m_media_is_present ? 2 : 0;
					break;

				case 0x86:
					// set audio volume
					m_volume = data;

					for (int i = 0; i < std::size(m_dmadac); i++)
						m_dmadac[i]->set_volume(m_volume);
					break;

				case 0x87:
					// ?
					if (data == 0x7f)
						m_mcu_response = 1;
					else if (m_media_is_hdd)
					{
						if (data == 0x1b || data == 0x1c)
							m_mcu_response = ~data & 1; // hack. absolutely not the right calculation
					}
					else
						m_mcu_response = 0;
					break;

				default:
					printf("BAM2 MCU param: %04x %04x (PC %08x)\n", m_mcu_command, data, m_maincpu->pc());
					break;
			}
		}
		break;

	case 3:
		// lamps
		break;

	default:
		printf("BAM2 MCU unk: %d %04x %04x (PC %08x)\n", offset, m_mcu_command, data, m_maincpu->pc());
		break;
	}
}

uint16_t bam2_hle_state::mcu_r(offs_t offset, uint16_t mem_mask)
{
	switch (offset)
	{
	case 0:
		// response unused, read directly after writing rom bank
		return 0;

	case 2:
		return m_mcu_response;

	case 3:
		// response unused, read directly after writing lamp values
		return 0;
	}

	return 0;
}

void bam2_hle_state::unk_w(offs_t offset, uint16_t data)
{
}

uint16_t bam2_hle_state::unk_r()
{
	return 0;
}

////////////////////////////////////////////////////////
// HDD
// TODO: The code probably breaks if the HDD has a unit size that isn't 512

bam2_hle_hdd_state::bam2_hle_hdd_state(const machine_config &mconfig, device_type type, const char *tag)
	: bam2_hle_state(mconfig, type, tag),
	m_image(nullptr)
{
}

void bam2_hle_hdd_state::bam2(machine_config &config)
{
	bam2_hle_state::bam2(config);

	ATA_INTERFACE(config, m_ata).options(ata_devices, "hdd", nullptr, true);
}

void bam2_hle_hdd_state::machine_start()
{
	bam2_hle_state::machine_start();

	save_item(NAME(m_sectors_per_cluster));
	save_item(NAME(m_bytes_per_sector));
	save_item(NAME(m_first_data_sector));
	save_item(NAME(m_audio_cluster));
}

void bam2_hle_hdd_state::machine_reset()
{
	bam2_hle_state::machine_reset();

	m_image = m_ata->subdevice<ata_slot_device>("0")->subdevice<ide_hdd_device>("hdd")->subdevice<harddisk_image_device>("image");
	m_media_is_present = m_image->exists();
	m_media_is_hdd = true;

	m_sectors_per_cluster = 0;
	m_bytes_per_sector = 0;
	m_first_data_sector = 0;
	m_audio_cluster = 0;

	m_file_records.clear();

	if (m_media_is_present)
		parse_filesystem();
}

void bam2_hle_hdd_state::parse_filesystem()
{
	#pragma pack(1)
	typedef struct mbr_partition_entry {
		uint8_t attribs;
		uint8_t start_chs[3];
		uint8_t partition_type;
		uint8_t last_chs[3];
		uint32_t start_lba;
		uint32_t sectorcnt;
	} mbr_partition_entry;

	typedef struct mbr_format {
		uint8_t bootstrap[440];
		uint32_t disk_id;
		uint16_t reserved;
		mbr_partition_entry partitions[4];
		uint16_t valid_bootloader_signature;
	} mbr_format;

	typedef struct fat32_boot_entry {
		uint8_t BS_jmpBoot[3];
		uint8_t BS_OEMName[8];
		uint16_t BPB_BytsPerSec;
		uint8_t BPB_SecPerClus;
		uint16_t BPB_RsvdSecCnt;
		uint8_t BPB_NumFATs;
		uint16_t BPB_RootEntCnt;
		uint16_t BPB_TotSec16;
		uint8_t BPB_Media;
		uint16_t BPB_FATSz16;
		uint16_t BPB_SecPerTrk;
		uint16_t BPB_NumHeads;
		uint32_t BPB_HiddSec;
		uint32_t BPB_TotSec32;
		uint32_t BPB_FATSz32;
		uint16_t BPB_ExtFlags;
		uint16_t BPB_FSVer;
		uint32_t BPB_RootClus;
		uint16_t BPB_FSInfo;
		uint16_t BPB_BkBootSec;
		uint8_t BPB_Reserved[12];
		uint8_t BS_DrvNum;
		uint8_t BS_Reserved1;
		uint8_t BS_BootSig;
		uint32_t BS_VolID;
		uint8_t BS_VolLab[11];
		uint8_t BS_FilSysType[8];
	} fat32_boot_entry;

	typedef struct fat32_dir_entry
	{
		char DIR_Name[11];
		uint8_t DIR_Attr;
		uint8_t DIR_NTRes;
		uint8_t DIR_CrtTimeTenth;
		uint16_t DIR_CrtTime;
		uint16_t DIR_CrtDate;
		uint16_t DIR_LstAccDate;
		uint16_t DIR_FstClusHI;
		uint16_t DIR_WrtTime;
		uint16_t DIR_WrtDate;
		uint16_t DIR_FstClusLO;
		uint32_t DIR_FileSize;
	} fat32_dir_entry;
	#pragma pack()

	auto *const chd = m_image->current_preset_image_chd();

	std::vector<uint8_t> mbr_buffer(chd->unit_bytes());
	mbr_format *const mbr = (mbr_format*)mbr_buffer.data();
	m_image->read(0, mbr_buffer.data());

	assert(mbr->valid_bootloader_signature == 0xaa55);

	// For now, only support the first partition since that's all bam2's HDD uses
	assert(mbr->partitions[0].partition_type == 0x0b); // is FAT32

	std::vector<uint8_t> bpb_buffer(chd->unit_bytes());
	fat32_boot_entry *const bpb = (fat32_boot_entry*)bpb_buffer.data();
	m_image->read(mbr->partitions[0].start_lba, bpb_buffer.data());

	m_sectors_per_cluster = bpb->BPB_SecPerClus;
	m_bytes_per_sector = bpb->BPB_BytsPerSec;

	const uint32_t fat_size = bpb->BPB_FATSz32 * bpb->BPB_BytsPerSec;
	const uint32_t fat_size_rounded = ((fat_size + chd->unit_bytes() - 1) / chd->unit_bytes()) * chd->unit_bytes();
	uint32_t fat_sec_chd_lba = (mbr->partitions[0].start_lba + bpb->BPB_RsvdSecCnt) * bpb->BPB_BytsPerSec / chd->unit_bytes();

	clusters.resize(fat_size_rounded / 4);
	for (int i = 0; i < fat_size; i += chd->unit_bytes())
		m_image->read(fat_sec_chd_lba++, (uint8_t*)clusters.data() + i);

	const uint32_t first_data_sec = mbr->partitions[0].start_lba + bpb->BPB_RsvdSecCnt + (bpb->BPB_NumFATs * bpb->BPB_FATSz32);
	uint32_t directory_lba = ((bpb->BPB_RootClus - 2) * bpb->BPB_SecPerClus) + first_data_sec;

	m_first_data_sector = first_data_sec;

	bool enumerating_files = true;
	while (enumerating_files)
	{
		std::vector<fat32_dir_entry> records(chd->unit_bytes() / sizeof(fat32_dir_entry));

		m_image->read(directory_lba++, &records[0]);

		for (int i = 0; i < records.size(); i++)
		{
			// The FAT directory is variable size but it's easier to cache the info so read it in a lazy way
			// Lazy way to detect last file
			if (strlen(records[i].DIR_Name) == 0)
			{
				enumerating_files = false;
				break;
			}

			for (int j = 0; j < std::size(records[i].DIR_Name); j++)
			{
				if (records[i].DIR_Name[j] == '.' || records[i].DIR_Name[j] == ' ')
				{
					records[i].DIR_Name[j] = '\0';
					break;
				}
			}

			const uint32_t filename_num = atoi(records[i].DIR_Name);
			m_file_records[filename_num] = fat32_file_record{
				static_cast<uint32_t>((records[i].DIR_FstClusHI << 16) | records[i].DIR_FstClusLO),
				records[i].DIR_FileSize
			};
		}
	}
}

bool bam2_hle_hdd_state::file_exists(uint32_t fileid)
{
	return m_file_records.find(fileid) != m_file_records.end();
}

void bam2_hle_hdd_state::play_audio(uint32_t fileid)
{
	if (!file_exists(fileid))
    {
		printf("Could not play requested file %d %d\n", fileid, m_fileid);
        return;
    }

    m_audio_remaining_samples = m_audio_read_bytes = 0;
    m_audio_cluster = m_file_records[fileid].cluster;
    m_audio_filesize = m_file_records[fileid].length;

    if (m_media_is_hdd)
    {
        m_audio_filesize /= 4;

        // follow the clusters until we're at the mirrored BGM
        const uint32_t clusters_per_file = m_audio_filesize / m_bytes_per_sector / m_sectors_per_cluster;
        for (int i = 0; i < m_mirror_idx; i++)
        {
            for (int j = 0; j < clusters_per_file; j++)
                m_audio_cluster = clusters[m_audio_cluster];
        }
    }

    for (int i = 0; i < std::size(m_dmadac); i++)
    {
        m_dmadac[i]->flush();
        m_dmadac[i]->enable(1);
    }

    m_audio_playing = true;
    m_audio_timer->adjust(attotime::from_hz(44100), 0, attotime::from_hz(44100));
}

TIMER_CALLBACK_MEMBER(bam2_hle_hdd_state::audio_playback)
{
	if ((m_audio_remaining_samples == 0 && (m_audio_read_bytes >= m_audio_filesize || m_audio_cluster >= 0x0ffffff7)) || !m_audio_playing)
	{
		for (int i = 0; i < std::size(m_dmadac); i++)
			m_dmadac[i]->enable(0);

		m_audio_timer->adjust(attotime::never);
		return;
	}

	if (m_audio_remaining_samples < DMADAC_MAX_SAMPLE_COUNT / 2 && m_audio_cluster < 0x0ffffff7)
	{
		const int cluster_read_count = (DMADAC_MAX_SAMPLE_COUNT - m_audio_remaining_samples) * sizeof(int16_t) * std::size(m_dmadac) / m_bytes_per_sector / m_sectors_per_cluster;
		std::vector<uint8_t> audio_buffer(m_bytes_per_sector * m_sectors_per_cluster * cluster_read_count);

		const uint32_t current_read_bytes = m_audio_read_bytes;
		uint32_t offs = 0;

		for (int j = 0; j < cluster_read_count; j++)
		{
			const uint32_t data_lba = (m_audio_cluster - 2) * m_sectors_per_cluster + m_first_data_sector;

			for (int i = 0; i < m_sectors_per_cluster; i++)
			{
				m_image->read(data_lba + i, &audio_buffer[offs]);
				offs += m_bytes_per_sector;
			}

			m_audio_read_bytes += m_bytes_per_sector * m_sectors_per_cluster;
			if (m_audio_read_bytes > m_audio_filesize)
			{
				m_audio_read_bytes = m_audio_filesize;
				break;
			}

			m_audio_cluster = clusters[m_audio_cluster];
			if (m_audio_cluster >= 0x0ffffff7)
				break;
		}

		const uint32_t read_bytes = m_audio_read_bytes - current_read_bytes;
		const int16_t *samples = (int16_t*)audio_buffer.data();
		const uint32_t samples_read = read_bytes / (sizeof(int16_t) * std::size(m_dmadac));
		for (int i = 0; i < std::size(m_dmadac); i++)
			m_dmadac[i]->transfer(i, 2, 2, samples_read, samples);

		m_audio_remaining_samples += samples_read;
	}

	m_audio_remaining_samples--;
}

////////////////////////////////////////////////////////
// CD-ROM

bam2_hle_cdrom_state::bam2_hle_cdrom_state(const machine_config &mconfig, device_type type, const char *tag)
	: bam2_hle_state(mconfig, type, tag),
	m_image(nullptr)
{
}

void bam2_hle_cdrom_state::bam2(machine_config &config)
{
	bam2_hle_state::bam2(config);

	ATA_INTERFACE(config, m_ata).options(ata_devices, "cdrom", nullptr, true);
}

void bam2_hle_cdrom_state::machine_start()
{
	bam2_hle_state::machine_start();

	save_item(NAME(m_audio_lba));
}

void bam2_hle_cdrom_state::machine_reset()
{
	bam2_hle_state::machine_reset();

	m_image = m_ata->subdevice<ata_slot_device>("0")->subdevice<ide_hdd_device>("cdrom")->subdevice<cdrom_image_device>("image");
	m_media_is_present = m_image->exists();
	m_media_is_hdd = false;

	m_audio_lba = 0;

	m_file_records.clear();

	if (m_media_is_present)
		parse_filesystem();
}

void bam2_hle_cdrom_state::parse_filesystem()
{
#pragma pack(1)
	// Structs copied from spicyjpeg's 573in1 tool
	struct ISOUint16
	{
		uint16_t le, be;
	};

	struct ISOUint32
	{
		uint32_t le, be;
	};

	struct ISODate {
		uint8_t year, month, day, hour, minute, second, timezone;
	};

	enum ISORecordFlag : uint8_t {
		ISO_RECORD_EXISTENCE    = 1 << 0,
		ISO_RECORD_DIRECTORY    = 1 << 1,
		ISO_RECORD_ASSOCIATED   = 1 << 2,
		ISO_RECORD_EXT_ATTR     = 1 << 3,
		ISO_RECORD_PROTECTION   = 1 << 4,
		ISO_RECORD_MULTI_EXTENT = 1 << 7
	};

	struct ISORecord {
		uint8_t   recordLength;        // 0x00
		uint8_t   extendedAttrLength;  // 0x01
		ISOUint32 lba;                 // 0x02-0x09
		ISOUint32 length;              // 0x0a-0x11
		ISODate   date;                // 0x12-0x18
		uint8_t   flags;               // 0x19
		uint8_t   interleaveLength;    // 0x1a
		uint8_t   interleaveGapLength; // 0x1b
		ISOUint16 volumeNumber;        // 0x1c-0x1f
		uint8_t   nameLength;          // 0x20
	};

	enum ISOVolumeDescType : uint8_t {
		ISO_TYPE_BOOT_RECORD      = 0x00,
		ISO_TYPE_PRIMARY          = 0x01,
		ISO_TYPE_SUPPLEMENTAL     = 0x02,
		ISO_TYPE_VOLUME_PARTITION = 0x03,
		ISO_TYPE_TERMINATOR       = 0xff
	};

	struct ISOPrimaryVolumeDesc {
		uint8_t type;     // 0x000
		uint8_t magic[5]; // 0x001-0x005
		uint8_t version;  // 0x006

		uint8_t   _reserved;
		char  system[32];            // 0x008-0x027
		char  volume[32];            // 0x028-0x047
		uint8_t   _reserved2[8];
		ISOUint32 volumeLength;          // 0x050-0x057
		uint8_t   _reserved3[32];
		ISOUint16 numVolumes;            // 0x078-0x07b
		ISOUint16 volumeNumber;          // 0x07c-0x07f
		ISOUint16 sectorLength;          // 0x080-0x083
		ISOUint32 pathTableLength;       // 0x084-0x08b
		uint32_t  pathTableLEOffsets[2]; // 0x08c-0x093
		uint32_t  pathTableBEOffsets[2]; // 0x094-0x09b
		ISORecord root;                  // 0x09c-0x0bc
		uint8_t   rootName;              // 0x09d
		char  volumeSet[128];        // 0x0be-0x13d
		char  publisher[128];        // 0x13e-0x1bd
		char  dataPreparer[128];     // 0x1be-0x23d
		char  application[128];      // 0x23e-0x2bd
		char  copyrightFile[37];     // 0x2be-0x2e2
		char  abstractFile[37];      // 0x2e3-0x307
		char  bibliographicFile[37]; // 0x308-0x32c
		uint8_t   creationDate[17];      // 0x32d-0x33d
		uint8_t   modificationDate[17];  // 0x33e-0x34e
		uint8_t   expirationDate[17];    // 0x34f-0x35f
		uint8_t   effectiveDate[17];     // 0x360-0x370
		uint8_t   isoVersion;            // 0x371
		uint8_t   _reserved4;
		uint8_t   extensionData[512];    // 0x373-0x572
		uint8_t   _reserved5[653];
	};
#pragma pack()

	const uint32_t _VOLUME_DESC_START_LBA = 0x10;
	const uint32_t _VOLUME_DESC_END_LBA   = 0x20;

	for (uint32_t lba = _VOLUME_DESC_START_LBA; lba < _VOLUME_DESC_END_LBA; lba++) {
		uint8_t pvdBuffer[2048];
		auto pvd = reinterpret_cast<const ISOPrimaryVolumeDesc *>(pvdBuffer);

		m_image->read_data(lba, pvdBuffer, cdrom_file::CD_TRACK_MODE1);

		if (pvd->magic[0] != 'C' || pvd->magic[1] != 'D' || pvd->magic[2] != '0' || pvd->magic[3] != '0' || pvd->magic[4] != '1')
			fatalerror("Not a valid iso9660 image\n");

		if (pvd->type == ISO_TYPE_TERMINATOR)
			break;
		if (pvd->type != ISO_TYPE_PRIMARY)
			continue;

		if (pvd->isoVersion != 1)
			fatalerror("unsupported ISO version 0x%02x", pvd->isoVersion);

		const auto numSectors = (pvd->root.length.le + 2047) / 2048;
		std::vector<uint8_t> rootData(numSectors * 2048);

		for (int i = 0; i < numSectors; i++)
		{
			m_image->read_data(pvd->root.lba.le, &rootData[i * 2048], cdrom_file::CD_TRACK_MODE1);
		}

		size_t offs = 0;
		while (true)
		{
			auto record = reinterpret_cast<const ISORecord *>(rootData.data() + offs);

			if (record->recordLength == 0)
				break;

			// All of the files we need are in the top directory so just ignore directories
			if (record->flags & ISO_RECORD_DIRECTORY)
			{
				offs += record->recordLength;
				continue;
			}


			char *filename = (char*)(rootData.data() + offs + sizeof(ISORecord));
			for (int i = 0; i < record->nameLength; i++)
			{
				if (filename[i] == '.')
				{
					filename[i] = '\0';
					break;
				}
			}

			const uint32_t filename_num = atoi(filename);
			m_file_records[filename_num] = iso9660_file_record{
				record->lba.le,
				record->length.le
			};

			offs += record->recordLength;
		}
	}
}

bool bam2_hle_cdrom_state::file_exists(uint32_t fileid)
{
	return m_file_records.find(fileid) != m_file_records.end();
}

void bam2_hle_cdrom_state::play_audio(uint32_t fileid)
{
	printf("Playing file %d %08x %08x\n", fileid, m_file_records[fileid].lba, m_file_records[fileid].length);

	if (!file_exists(fileid))
	{
		printf("Could not play requested file %d, out of bounds (%zu)\n", m_fileid, m_file_records.size());
		return;
	}

    m_audio_remaining_samples = m_audio_read_bytes = 0;
	m_audio_lba = m_file_records[fileid].lba;
    m_audio_filesize = m_file_records[fileid].length;

    for (int i = 0; i < std::size(m_dmadac); i++)
    {
        m_dmadac[i]->flush();
        m_dmadac[i]->enable(1);
    }

    m_audio_playing = true;
    m_audio_timer->adjust(attotime::from_hz(44100), 0, attotime::from_hz(44100));
}

TIMER_CALLBACK_MEMBER(bam2_hle_cdrom_state::audio_playback)
{
	if ((m_audio_remaining_samples == 0 && m_audio_read_bytes >= m_audio_filesize) || !m_audio_playing)
	{
		for (int i = 0; i < std::size(m_dmadac); i++)
			m_dmadac[i]->enable(0);

		m_audio_timer->adjust(attotime::never);
		return;
	}

	if (m_audio_remaining_samples < DMADAC_MAX_SAMPLE_COUNT / 2)
	{
		constexpr int SECTOR_LENGTH = 2048;
		const int sector_read_count = (DMADAC_MAX_SAMPLE_COUNT - m_audio_remaining_samples) * sizeof(int16_t) * std::size(m_dmadac) / SECTOR_LENGTH;
		std::vector<uint8_t> audio_buffer(SECTOR_LENGTH * sector_read_count);

		const uint32_t start_read_bytes = m_audio_read_bytes;

		for (int i = 0; i < sector_read_count; i++)
		{
			m_image->read_data(m_audio_lba++, &audio_buffer[i * SECTOR_LENGTH], cdrom_file::CD_TRACK_MODE1);

			m_audio_read_bytes += SECTOR_LENGTH;
			if (m_audio_read_bytes + SECTOR_LENGTH > m_audio_filesize)
			{
				m_audio_read_bytes = m_audio_filesize;
				break;
			}
		}

		const uint32_t read_bytes = m_audio_read_bytes - start_read_bytes;
		const int16_t *samples = (int16_t*)audio_buffer.data();
		const uint32_t samples_read = read_bytes / (sizeof(int16_t) * std::size(m_dmadac));
		for (int i = 0; i < std::size(m_dmadac); i++)
			m_dmadac[i]->transfer(i, 2, 2, samples_read, samples);

		m_audio_remaining_samples += samples_read;
	}

	m_audio_remaining_samples--;
}