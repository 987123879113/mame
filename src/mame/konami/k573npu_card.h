// license:BSD-3-Clause
// copyright-holders:windyfairy
#ifndef MAME_KONAMI_K573NPU_CARD_H
#define MAME_KONAMI_K573NPU_CARD_H

#pragma once

#include "emu.h"

#include <deque>

#include "imagedev/harddriv.h"
#include "bus/pccard/pccard.h"

#include "asio.h"

class konami_573_network_pcb_unit_hdd
{
public:
	konami_573_network_pcb_unit_hdd(harddisk_image_device *hdd);

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
	int format(uint32_t startLba, uint32_t partitionCount1, uint32_t partitionCount2, uint32_t param4, uint32_t param5);

private:
	enum {
		SECTOR_SIZE = 0x200,
		START_SECTOR = 8,

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

	int parse_filesystem();
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

	harddisk_image_device *m_hdd;
	uint64_t m_hdd_offset;

	partition_t m_partition;
	uint32_t m_next_fd;
	uint32_t m_mount_size_sectors;

	std::unordered_map<uint32_t, file_desc_t> m_filedescs;
};

template <typename T, uint32_t Size>
class ringbuffer
{
public:
	ringbuffer()
	{
		clear();
	}

	ringbuffer(ringbuffer &in, std::size_t len)
	{
		clear();

		// copy existing buffer data
		for (auto i = 0; i < len; i++)
			push(in.at(i));
	}

	void clear()
	{
		std::fill(std::begin(m_buf), std::end(m_buf), 0);
		m_head = m_tail = 0;
		m_is_locked = false;
	}

	bool is_locked()
	{
		return m_is_locked;
	}

	bool lock()
	{
		if (m_is_locked)
			return false;

		m_is_locked = true;
		m_head_locked = m_head;
		m_tail_locked = m_tail;

		return true;
	}

	bool unlock()
	{
		m_is_locked = false;
		return true;
	}

	void push(const T val)
	{
		m_buf[m_tail] = val;
		m_tail = (m_tail + 1) % count();
		// printf("inner m_buf_tail: %04zx\n", m_buf_tail);
	}

	T pop()
	{
		T val = m_buf[m_head];
		m_head = (m_head + 1) % count();
		return val;
	}

	T peek()
	{
		return m_buf[m_head];
	}

	T at(const std::size_t offset)
	{
		return m_buf[(m_head + offset) % count()];
	}

	std::size_t head()
	{
		return m_head * sizeof(T);
	}

	std::size_t tail()
	{
		return m_tail * sizeof(T);
	}

	std::size_t head_locked()
	{
		if (!m_is_locked)
			return head();

		return m_head_locked * sizeof(T);
	}

	std::size_t tail_locked()
	{
		if (!m_is_locked)
			return tail();

		return m_tail_locked * sizeof(T);
	}

	void head(const std::size_t val)
	{
		m_head = val % count();

		if (m_head > m_tail)
			m_tail = m_head;
	}

	void tail(const std::size_t val)
	{
		m_tail = val % count();
	}

	std::size_t length()
	{
		return length_from(m_head, m_tail);
	}

	std::size_t length_locked()
	{
		if (!m_is_locked)
			return length();

		return length_from(m_head_locked, m_tail_locked);
	}

	std::size_t length_from(const std::size_t start, const std::size_t end)
	{
		std::size_t r;
		if (end < start)
			r = (end + count()) - start;
		else
			r = end - start;
		return r * sizeof(T);
	}

	bool empty()
	{
		return m_tail == m_head;
	}

	constexpr std::size_t size()
	{
		return Size;
	}

	constexpr std::size_t count()
	{
		return Size / sizeof(T);
	}

private:
	std::array<T, Size / sizeof(T)> m_buf;
	std::size_t m_head, m_tail;
	std::size_t m_head_locked, m_tail_locked;
	bool m_is_locked;
};

class konami_573_network_pcb_unit_pccard_device : public device_t,
	public device_pccard_interface
{
public:
	konami_573_network_pcb_unit_pccard_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	virtual uint16_t read_memory(offs_t offset, uint16_t mem_mask = ~0) override;
	virtual void write_memory(offs_t offset, uint16_t data, uint16_t mem_mask = ~0) override;

	virtual uint16_t read_reg(offs_t offset, uint16_t mem_mask = ~0) override;
	virtual void write_reg(offs_t offset, uint16_t data, uint16_t mem_mask = ~0) override;

protected:
	virtual void device_start() override;
	virtual void device_stop() override;
	virtual void interface_post_start() override;
	virtual void device_reset() override;
	virtual void device_add_mconfig(machine_config &config) override;

private:
	enum {
		FIFO_SIZE = 0x8000,
	};

	struct {
		uint8_t npu_id[8];
		uint8_t mac_address[6];
		uint8_t ip_address[4];
		uint8_t subnet_mask[4];
		uint8_t default_gateway[4];
		uint8_t dns_server1[4];
		uint8_t dns_server2[4];
		uint8_t dhcp_server[4];
		uint8_t ntp_server[4];
		char domain_name[32];
	} config;

	void process_opcode();
	void process_opcode_internal(ringbuffer<uint16_t, 0x10000> &input_buf, ringbuffer<uint16_t, 0x10000> &output_buf);
	void parse_ini_file(const char *filename);

	uint8_t str2hex(const char *input, int len);
	void parse_hexstr(const char *input, uint8_t* output, size_t outputlen);
	void parse_mac_address(const char *input, uint8_t* output, size_t outputlen);
	void parse_ip_address(const char *input, uint8_t* output, size_t outputlen);
	uint32_t parse_ip_address(const char *input);

	size_t read_fifo_bytes(ringbuffer<uint16_t, 0x10000> &buf, uint8_t *output, size_t len);
	size_t read_fifo_bytes(ringbuffer<uint16_t, 0x10000> &buf, char *output, size_t len) { return read_fifo_bytes(buf, (uint8_t*)output, len); }
	uint16_t read_fifo_u16(ringbuffer<uint16_t, 0x10000> &buf);
	uint32_t read_fifo_u32(ringbuffer<uint16_t, 0x10000> &buf);

	void write_fifo_bytes(ringbuffer<uint16_t, 0x10000> &buf, const uint8_t *input, size_t len);
	void write_fifo_bytes(ringbuffer<uint16_t, 0x10000> &buf, const char *input, size_t len) { return write_fifo_bytes(buf, (const uint8_t*)input, len); }
	void write_fifo_u16(ringbuffer<uint16_t, 0x10000> &buf, uint16_t val);
	void write_fifo_u32(ringbuffer<uint16_t, 0x10000> &buf, uint32_t val);

	required_device<harddisk_image_device> m_npu_hdd;

	uint16_t m_val_unk;
	uint32_t m_init_cur_step;

	int32_t m_buffer_write_length, m_buffer_read_length;

	uint16_t m_state_requested, m_state_cur;
	uint16_t m_packet_id;

	ringbuffer<uint16_t, 0x10000> m_output_buf;
	ringbuffer<uint16_t, 0x10000> m_input_buf;

	std::unique_ptr<konami_573_network_pcb_unit_hdd> m_hdd;

	asio::io_service m_io_service;
	std::thread m_thread;
};

DECLARE_DEVICE_TYPE(KONAMI_573_NETWORK_PCB_UNIT_PCCARD, konami_573_network_pcb_unit_pccard_device)

#endif // MAME_KONAMI_K573NPU_CARD_H
