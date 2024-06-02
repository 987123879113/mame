// license:BSD-3-Clause
// copyright-holders:windyfairy
#ifndef MAME_KONAMI_K573NPU_CARD_PYTHONFS_H
#define MAME_KONAMI_K573NPU_CARD_PYTHONFS_H

#include "imagedev/harddriv.h"

class konami_573_network_pcb_unit_storage
{
public:
	konami_573_network_pcb_unit_storage(uint32_t size);
	konami_573_network_pcb_unit_storage(harddisk_image_device *hdd);

	void dump();

	bool exists();
	bool set_block_size(uint32_t blocksize);
	bool read(uint32_t lbasector, void *buffer);
	bool write(uint32_t lbasector, const void *buffer);

private:
	util::core_file::ptr m_ptr;

	harddisk_image_device *m_hdd;
	std::unique_ptr<uint8_t[]> m_data;

	uint32_t m_blocksize;
	uint32_t m_storage_size;
};

class konami_573_network_pcb_unit_storage_pythonfs
{
public:
	konami_573_network_pcb_unit_storage_pythonfs(uint32_t fd_mask, konami_573_network_pcb_unit_storage *hdd);

	void reset();

	int open(const char *path, uint32_t flags, uint32_t mode);
	int close(int fd, bool allow_write);
	int close(int fd) { return close(fd, true); }
	int read(int fd, uint32_t len, uint8_t *outbuf, uint32_t &read_count);
	int write(int fd, uint32_t len, uint8_t *data);
	int lseek(int fd, uint32_t offset, int whence);
	// int ioctl();
	int dopen(const char *path);
	int dclose(int fd);
	int dread(int fd, uint8_t *outbuf);
	int remove(const char *path);
	int mkdir(const char *path, int mode);
	int rmdir(const char *path);
	int getstat(const char *path, uint8_t *outbuf);
	// int chstat();
	// int rename(const char *old_filename, const char *new_filename);
	int chdir(const char *path);
	int mount(const char *path);
	int umount(const char *path);
	int devctl(uint32_t reqtype, uint32_t param2, uint32_t resplen, uint8_t *data);
	int format(const char *path, uint32_t start_lba, uint32_t partition_count1, uint32_t partition_count2, uint32_t param4, uint32_t param5);

private:
	enum {
		SECTOR_SIZE = 0x200,

		BLOCK_END = 0x00ffffff,
	};

	enum {
		ATTRIB_IS_FOLDER = 1 << 24,
	};

	enum {
		FILE_FLAG_WRITE = 0x02,
	};

	enum {
		SEARCH_MODE_CONTIGUOUS = 1 << 0,
		SEARCH_MODE_ANY = 1 << 1,

		SEARCH_START_NONE = 0, // 0 is never a valid sector so ignore it
	};

#ifdef _MSC_VER
#pragma pack(push,1)
#define NPU_STRUCT_PACKED
#else
#define NPU_STRUCT_PACKED __attribute__((packed))
#endif

	typedef struct NPU_STRUCT_PACKED {
		uint8_t padding[4];
		uint8_t magic[8];
		uint8_t magic2[8]; // magic ^ 0xff
		uint8_t partitionCount1;
		uint8_t partitionCount2;
		uint8_t blockSizeMult;
		uint8_t nodeBlockTableCount;
		uint32_t partitionTotalSectorCount;
		uint32_t nodeBlockTableSectorCount;
		uint32_t dataSectorOffset; // nodeBlockTableEntryCount * nodeBlockTableCount + 1
		uint32_t unk1; // ???
	} partition_header_t;

	typedef struct NPU_STRUCT_PACKED {
		char name[20];
		uint32_t size;
		uint32_t unk;
		uint32_t offset;
	} directory_t;

	typedef struct NPU_STRUCT_PACKED {
		uint32_t mode;
		uint32_t attr;
		uint32_t size;
		uint8_t ctime[8];
		uint8_t atime[8];
		uint8_t mtime[8];
		uint32_t hsize;
		uint32_t priv0;
		uint32_t priv1;
		uint32_t priv2;
		uint32_t priv3;
		uint32_t priv4;
		uint32_t priv5;
	} stat_t;

	typedef struct NPU_STRUCT_PACKED {
		stat_t stat;
		char name[256];
		uint32_t unk;
	} dirent_t;

#ifdef _MSC_VER
#pragma pack(pop)
#endif

	typedef struct {
		uint32_t fd;
		bool is_open;
		bool is_dir;
		uint32_t flags;
		uint32_t mode;

		uint32_t curblock;
		uint32_t block_offset;
		uint32_t block_suboffset;

		directory_t entry;
		std::vector<directory_t> directory;

		std::string path;
		std::string filename;
	} file_desc_t;

	typedef struct {
		partition_header_t header;
		uint32_t *blocks;

		std::string directory_path;
		std::vector<directory_t> directory;

		size_t offset;
		uint32_t directory_block_sector; // 0 is not valid
		uint32_t blockSize;

		bool is_loaded;
	} partition_t;

	void write_some_at(std::uint64_t offset, void *buffer, std::size_t length, std::size_t &actual);
	void read_some_at(std::uint64_t offset, void *buffer, std::size_t length, std::size_t &actual);
	void read_some(void *buffer, std::size_t length, std::size_t &actual);
	void seek(std::int64_t offset, int whence);

	int parse_filesystem(int start_sector);
	int chdir(const char *path, std::vector<directory_t> &output);

	int mkdir_internal(const char *path, const char *folder);
	int get_available_block_offsets(size_t size, int mode, uint32_t start, std::vector<uint32_t> &blockList);
	int write_directories(std::vector<directory_t> dir, std::vector<uint32_t> &blockList);
	uint32_t get_raw_offset_from_block_offset(uint32_t offset);
	uint32_t get_lba_from_block_offset(uint32_t offset);
	void write_block_table();
	uint32_t get_lba_from_file_offset(uint32_t block_offset, uint32_t offset);

	int add_new_entry_to_directory(const char *path, directory_t &entry);
	int remove_entry_from_directory(const char *path, const char *name);

	bool is_valid_fd(int fd);
	uint32_t get_next_fd();

	konami_573_network_pcb_unit_storage *m_dev;

	uint64_t m_hdd_offset;

	partition_t m_partition;
	uint32_t m_next_fd;

	uint32_t m_fd_mask;

	std::unordered_map<uint32_t, file_desc_t> m_filedescs;
};

#endif // MAME_KONAMI_K573NPU_CARD_PYTHONFS_H
