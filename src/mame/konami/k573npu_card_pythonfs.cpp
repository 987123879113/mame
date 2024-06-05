// license:BSD-3-Clause
// copyright-holders:windyfairy

#include "emu.h"

#include "k573npu_card.h"

#include "corestr.h"
#include "osdcomm.h"
#include "osdcore.h"
#include "utilfwd.h"

#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <system_error>

#define LOG_FS (1U << 1)

#define VERBOSE (LOG_GENERAL | LOG_FS)
#define LOG_OUTPUT_FUNC osd_printf_info

#include "logmacro.h"

#define LOGFS(...)         LOGMASKED(LOG_FS, __VA_ARGS__)


konami_573_network_pcb_unit_storage::konami_573_network_pcb_unit_storage(uint32_t size)
	: m_hdd(nullptr)
	, m_blocksize(512)
	, m_storage_size(size)
{
	m_data = std::make_unique<uint8_t[]>(m_storage_size);
	std::fill_n(m_data.get(), m_storage_size, 0);
}

void konami_573_network_pcb_unit_storage::dump()
{
	if (m_data != nullptr)
	{
		FILE *f = fopen("memdump.bin", "wb");
		fwrite(m_data.get(), 1, m_storage_size, f);
		fclose(f);
	}
}

konami_573_network_pcb_unit_storage::konami_573_network_pcb_unit_storage(harddisk_image_device *hdd)
	: m_hdd(hdd)
	, m_data(nullptr)
	, m_blocksize(512)
{
}

bool konami_573_network_pcb_unit_storage::exists()
{
	return m_hdd != nullptr || m_data != nullptr;
}

bool konami_573_network_pcb_unit_storage::set_block_size(uint32_t blocksize)
{
	if (!exists())
		return false;

	if (m_hdd != nullptr)
		return m_hdd->set_block_size(blocksize);

	m_blocksize = blocksize;

	return true;
}

bool konami_573_network_pcb_unit_storage::read(uint32_t lbasector, void *buffer)
{
	if (!exists())
		return false;

	if (m_hdd != nullptr)
		return m_hdd->read(lbasector, buffer);

	if (m_blocksize * lbasector >= m_storage_size || buffer == nullptr)
		return false;

	memset(buffer, 0, m_blocksize);
	memcpy(buffer, m_data.get() + m_blocksize * lbasector, m_blocksize);

	return true;
}

bool konami_573_network_pcb_unit_storage::write(uint32_t lbasector, const void *buffer)
{
	if (!exists())
		return false;

	if (m_hdd != nullptr)
		return m_hdd->write(lbasector, buffer);

	if (m_blocksize * lbasector >= m_storage_size || buffer == nullptr)
		return false;

	memset(m_data.get() + m_blocksize * lbasector, 0, m_blocksize);
	memcpy(m_data.get() + m_blocksize * lbasector, buffer, m_blocksize);

	return true;
}

////////

enum
{
	COMMAND_UNSUCCESSFUL = -2,
};


konami_573_network_pcb_unit_storage_pythonfs::konami_573_network_pcb_unit_storage_pythonfs(uint32_t fd_mask, konami_573_network_pcb_unit_storage *hdd)
	: m_dev(hdd)
	, m_fd_mask(fd_mask)
{
	m_partition = {};
	m_next_fd = fd_mask;
	m_filedescs.clear();
}

void konami_573_network_pcb_unit_storage_pythonfs::reset()
{
	if (m_dev && m_dev->exists())
		m_dev->set_block_size(SECTOR_SIZE);

	m_partition = {};
	m_hdd_offset = 0;

	m_next_fd = m_fd_mask;
	m_filedescs.clear();
}

void konami_573_network_pcb_unit_storage_pythonfs::write_some_at(std::uint64_t offset, void *buffer, std::size_t length, std::size_t &actual)
{
	if (!m_dev || !m_dev->exists())
		return;

	uint8_t sector[SECTOR_SIZE];

	actual = 0;

	// Handle partial sector first
	if ((offset % SECTOR_SIZE) != 0)
	{
		const auto size = std::min<size_t>(
			SECTOR_SIZE - (offset % SECTOR_SIZE),
			length - actual
		);

		std::fill(std::begin(sector), std::end(sector), 0);

		m_dev->read(offset / SECTOR_SIZE, sector);
		memcpy(sector + (offset % SECTOR_SIZE), &((uint8_t*)buffer)[actual], size);

		m_dev->write(offset / SECTOR_SIZE, sector);

		actual += size;
		offset += size;
	}

	while (actual < length)
	{
		const auto size = std::min<size_t>(
			SECTOR_SIZE,
			length - actual
		);

		std::fill(std::begin(sector), std::end(sector), 0);

		memcpy(sector, &((uint8_t*)buffer)[actual], size);

		m_dev->write(offset / SECTOR_SIZE, sector);

		actual += size;
		offset += size;
	}
}

void konami_573_network_pcb_unit_storage_pythonfs::read_some_at(std::uint64_t offset, void *buffer, std::size_t length, std::size_t &actual)
{
	if (!m_dev || !m_dev->exists())
		return;

	uint8_t sector[SECTOR_SIZE];

	actual = 0;

	while (actual < length)
	{
		const auto size = std::min<size_t>(SECTOR_SIZE - (offset % SECTOR_SIZE), length - actual);
		m_dev->read(offset / SECTOR_SIZE, sector);
		memcpy(&((uint8_t*)buffer)[actual], &sector[offset % SECTOR_SIZE], size);
		actual += size;
		offset += size;
	}
}

void konami_573_network_pcb_unit_storage_pythonfs::read_some(void *buffer, std::size_t length, std::size_t &actual)
{
	if (!m_dev || !m_dev->exists())
		return;

	uint8_t sector[SECTOR_SIZE];

	actual = 0;

	while (actual < length)
	{
		const auto size = std::min<size_t>(SECTOR_SIZE - (m_hdd_offset % SECTOR_SIZE), length - actual);
		m_dev->read(m_hdd_offset / SECTOR_SIZE, sector);
		memcpy(&((uint8_t*)buffer)[actual], &sector[m_hdd_offset % SECTOR_SIZE], size);
		actual += size;
		m_hdd_offset += size;
	}
}

void konami_573_network_pcb_unit_storage_pythonfs::seek(std::int64_t offset, int whence)
{
	if (whence == SEEK_SET)
		m_hdd_offset = offset;
	else if (whence == SEEK_CUR)
		m_hdd_offset += offset;
}

bool konami_573_network_pcb_unit_storage_pythonfs::is_valid_fd(int fd)
{
	return m_filedescs.find(fd) != m_filedescs.end() && m_filedescs[fd].is_open;
}

uint32_t konami_573_network_pcb_unit_storage_pythonfs::get_next_fd()
{
	uint32_t fd = m_next_fd;

	while (m_filedescs.find(fd) != m_filedescs.end())
		fd++;

	m_next_fd = fd + 1;

	return fd;
}

int konami_573_network_pcb_unit_storage_pythonfs::parse_filesystem(int start_sector)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	// TODO: This only currently handles one partition chunk, but there can be multiple.
	// Never seen it use multiple partitions so not sure how it's handled yet.
	uint64_t next_partition = start_sector * SECTOR_SIZE;
	size_t bytes_read = 0;

	if (m_partition.blocks != nullptr)
	{
		free(m_partition.blocks);
		m_partition.blocks = nullptr;
	}

	m_partition.is_loaded = false;

	m_partition.offset = next_partition;

	read_some_at(next_partition, &m_partition.header, sizeof(partition_header_t), bytes_read);
	m_partition.header.partitionTotalSectorCount = big_endianize_int32(m_partition.header.partitionTotalSectorCount);
	m_partition.header.nodeBlockTableSectorCount = big_endianize_int32(m_partition.header.nodeBlockTableSectorCount);
	m_partition.header.dataSectorOffset = big_endianize_int32(m_partition.header.dataSectorOffset);
	m_partition.header.unk1 = big_endianize_int32(m_partition.header.unk1);
	m_partition.blockSize = (1 << m_partition.header.blockSizeMult) * SECTOR_SIZE;

	if (bytes_read != sizeof(partition_header_t))
		return -1;

	if (memcmp(m_partition.header.magic, "PythonFS", 8) != 0)
		return -2;
	if (memcmp(m_partition.header.magic2, "\xaf\x86\x8b\x97\x90\x91\xb9\xac", 8) != 0)
		return -3;

	const auto nodeBlockTableCount = m_partition.header.nodeBlockTableSectorCount * SECTOR_SIZE / 4;
	m_partition.blocks = new uint32_t[nodeBlockTableCount];

	seek(next_partition + (1 * SECTOR_SIZE), 0); // seek to node blocks table

	for (int i = 0; i < nodeBlockTableCount; i++)
	{
		uint32_t val = 0;
		read_some(&val, sizeof(uint32_t), bytes_read);

		if (bytes_read != sizeof(uint32_t))
			return -4;

		m_partition.blocks[i] = big_endianize_int32(val);
	}

	m_partition.is_loaded = true;

	if (chdir("/") < 0)
		return -5;

	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::mount(const char *path)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	auto r = parse_filesystem(8);
	if (r < 0)
		r = parse_filesystem(0);

	return r;
}

int konami_573_network_pcb_unit_storage_pythonfs::umount(const char *path)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	// TODO: Clean up mounted partition state
	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::open(const char *path, uint32_t flags, uint32_t mode)
{
	if (!m_dev || !m_dev->exists())
		return -10;

	if (path == nullptr)
		return -2;

	if (!m_partition.is_loaded)
		return -3;

	// chdir to path if needed
	char *pathptr = (char*)path;
	size_t pathoffs = 0;

	while (strlen(pathptr) > 0 && (pathptr = strstr((char*)pathptr, "/")) != nullptr)
	{
		pathptr++;
		pathoffs = pathptr - path;
	}

	std::string curpath = m_partition.directory_path;
	if (pathoffs > 0)
	{
		if (path[0] == '/')
			curpath = std::string(path, pathoffs);
		else
			curpath += std::string(path, pathoffs);
	}

	std::vector<directory_t> directory;
	if (!curpath.empty())
	{
		if (chdir(curpath.c_str(), directory) < 0)
			return -4;
	}
	else
	{
		directory = m_partition.directory;
	}

	// TODO: handle flags and mode
	for (auto f : directory)
	{
		if (strcmp(f.name, path + pathoffs) == 0 && !(flags & 0x200)) // TODO: What's 0x200 and 0x100?
		{
			const uint32_t fd = get_next_fd();

			m_filedescs[fd] = file_desc_t{
				.fd = fd,
				.is_open = true,
				.is_dir = false,
				.flags = flags,
				.mode = mode,
				.curblock = f.offset & 0xffffff,
				.block_offset = 0,
				.block_suboffset = 0,
				.entry = f,
				.path = curpath,
				.filename = std::string(path + pathoffs),
			};

			return fd;
		}
	}

	if (flags & FILE_FLAG_WRITE)
	{
		const uint32_t fd = get_next_fd();

		std::vector<uint32_t> blocks;
		if (get_available_block_offsets(SECTOR_SIZE, SEARCH_MODE_ANY, SEARCH_START_NONE, blocks) < 0)
			return -1;

		directory_t f = {
			.size = 0,
			.unk = 0x1dfe200, // ?
			.offset = blocks[0] & 0xffffff,
		};
		strcpy(f.name, path + pathoffs);

		m_filedescs[fd] = file_desc_t{
			.fd = fd,
			.is_open = true,
			.is_dir = false,
			.flags = flags,
			.mode = mode,
			.curblock = f.offset & 0xffffff,
			.block_offset = 0,
			.block_suboffset = 0,
			.entry = f,
			.path = curpath,
			.filename = std::string(path + pathoffs),
		};

		return fd;
	}

	return -5;
}

int konami_573_network_pcb_unit_storage_pythonfs::close(int fd, bool allow_write)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	if (allow_write && m_filedescs[fd].flags & FILE_FLAG_WRITE)
	{
		if (m_filedescs[fd].path == "")
			m_filedescs[fd].path = m_partition.directory_path;

		add_new_entry_to_directory(m_filedescs[fd].path.c_str(), m_filedescs[fd].entry);
	}

	// TODO: Clean up state here in case anything is referencing it
	m_filedescs[fd].is_open = false;

	m_filedescs.erase(fd);

	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::lseek(int fd, uint32_t offset, int whence)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -5;

	if (!is_valid_fd(fd))
		return -2;

	auto &filedesc = m_filedescs[fd];

	if (whence == SEEK_SET)
	{
		if (offset > filedesc.entry.size)
			return -3;

		filedesc.block_offset = offset;
	}
	else if (whence == SEEK_CUR)
	{
		if (offset + filedesc.block_offset > filedesc.entry.size)
			return -3;

		filedesc.block_offset += offset;
	}
	else if (whence == SEEK_END)
	{
		// TODO: This probably has an off by 1 issue, untested
		if (offset > filedesc.entry.size)
			return -3;

		filedesc.block_offset = filedesc.entry.size - offset;
	}

	filedesc.block_suboffset = filedesc.block_offset % m_partition.blockSize;

	// TODO:
	uint32_t skiplen = 0;
	filedesc.curblock = filedesc.entry.offset & 0xffffff;
	while (skiplen + m_partition.blockSize <= filedesc.block_offset)
	{
		skiplen += m_partition.blockSize;

		if (m_partition.blocks[filedesc.curblock] == 0xffffff || m_partition.blocks[filedesc.curblock] == 0)
		{
			if (skiplen >= filedesc.block_offset)
				break;

			return -4;
		}

		filedesc.curblock = m_partition.blocks[filedesc.curblock];
	}

	return filedesc.block_offset;
}

int konami_573_network_pcb_unit_storage_pythonfs::chdir(const char *path)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	if (path[0] == '/')
	{
		m_partition.directory_path = path;
	}
	else
	{
		if (m_partition.directory_path.back() != '/')
			m_partition.directory_path += "/";
		m_partition.directory_path += path;
	}

	return chdir(m_partition.directory_path.c_str(), m_partition.directory);
}

int konami_573_network_pcb_unit_storage_pythonfs::chdir(const char *path, std::vector<directory_t> &output)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -2;

	std::vector<std::string> parts;

	if (path == nullptr)
		return 0;

	if (strcmp(path, "/") == 0)
	{
		parts.push_back("/");
	}
	else
	{
		int idx = 0;

		// handle / at beginning of path
		if (path[idx] == '/')
		{
			parts.push_back("/");
			while(idx < strlen(path) && path[idx] == '/')
				idx++;
		}

		int lastidx = idx;
		while (idx < strlen(path))
		{
			if (path[idx] == '/')
			{
				if (idx == lastidx)
					break;

				parts.push_back(std::string(path + lastidx, idx - lastidx));

				while(idx < strlen(path) && path[idx] == '/')
					idx++;

				lastidx = idx;
			}
			else
			{
				idx++;
			}
		}

		if (idx - lastidx > 0 && lastidx < strlen(path))
			parts.push_back(std::string(path + lastidx, idx - lastidx));
	}

	auto directory_block_sector = 1;
	for (auto s : parts)
	{
		if (s == "/")
		{
			directory_block_sector = 1;
		}
		else
		{
			bool found = false;

			// Find path in current directory list
			for (auto f : output)
			{
				if (strcmp(f.name, s.c_str()) == 0)
				{
					found = true;
					if ((f.offset & ATTRIB_IS_FOLDER) != ATTRIB_IS_FOLDER)
						return -3; // can't chdir into a file
					else
						directory_block_sector = f.offset & 0xffffff;
					break;
				}
			}

			if (!found)
				return -4;
		}

		output.clear();

		while (true)
		{
			for (int i = 0; i < m_partition.blockSize / sizeof(directory_t); i++)
			{
				directory_t dir = {};
				size_t bytes_read = 0;

				read_some_at(
					get_raw_offset_from_block_offset(directory_block_sector) + (i * sizeof(directory_t)),
					&dir,
					sizeof(directory_t),
					bytes_read
				);

				if (bytes_read != sizeof(directory_t) || strlen(dir.name) == 0)
					break;

				dir.offset = big_endianize_int32(dir.offset);
				dir.size = big_endianize_int32(dir.size);
				dir.unk = big_endianize_int32(dir.unk);

				// printf("Entry #%d:\n", i);
				// printf("\tName: %s\n", dir.name);
				// printf("\tOffset: %08x\n", dir.offset);
				// printf("\tSize: %08x\n", dir.size);
				// printf("\tAttrib: %02x\n", BIT(dir.offset, 24, 8));
				// printf("\tUnk: %08x\n", dir.unk);

				output.push_back(dir);
			}

			if (m_partition.blocks[directory_block_sector] == 0xffffff || m_partition.blocks[directory_block_sector] == 0)
				break;

			directory_block_sector = m_partition.blocks[directory_block_sector];
		}

		if (output.size() < 2)
			return -5; // must have at least . and .. as entries
	}

	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::read(int fd, uint32_t len, uint8_t *outbuf, uint32_t &read_count)
{
	read_count = 0;

	if (!m_dev || !m_dev->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	if (!is_valid_fd(fd))
		return -2;

	auto &filedesc = m_filedescs[fd];

	if ((filedesc.flags & 0x01) == 0 || filedesc.entry.size - filedesc.block_offset <= 0)
		return 0;

	while (read_count < len)
	{
		if (lseek(fd, filedesc.block_offset, 0) < 0)
			break;

		size_t bytes_read = 0;
		const auto readlen = std::min<uint32_t>(
			std::min<uint32_t>(len - read_count, m_partition.blockSize - filedesc.block_suboffset),
			filedesc.entry.size - filedesc.block_offset
		);

		if (readlen <= 0)
			break;

		const auto readoffs = get_raw_offset_from_block_offset(filedesc.curblock) + filedesc.block_suboffset;
		read_some_at(
			readoffs,
			outbuf + read_count,
			readlen,
			bytes_read
		);

		read_count += bytes_read;
		filedesc.block_offset += bytes_read;
	}

	return read_count;
}

int konami_573_network_pcb_unit_storage_pythonfs::write(int fd, uint32_t len, uint8_t *data)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	if (!is_valid_fd(fd))
		return -2;

	auto &filedesc = m_filedescs[fd];

	if ((filedesc.flags & FILE_FLAG_WRITE) == 0)
		return -1;

	size_t written_len = 0;
	uint8_t *block = (uint8_t*)calloc(m_partition.blockSize, sizeof(uint8_t));

	// Write remainder of current sector
	if ((filedesc.block_offset % m_partition.blockSize) > 0)
	{
		auto write_len = std::min<size_t>(
			m_partition.blockSize - (filedesc.block_offset % m_partition.blockSize),
			len
		);

		const auto offs = get_raw_offset_from_block_offset(filedesc.curblock) + filedesc.block_suboffset;

		size_t written = 0;
		write_some_at(offs, data, write_len, written);

		len -= written;
		written_len += written;
		filedesc.block_suboffset += written;
		filedesc.block_offset += written;

		if (filedesc.block_suboffset >= m_partition.blockSize)
		{
			filedesc.block_suboffset %= m_partition.blockSize;
			filedesc.curblock = m_partition.blocks[filedesc.curblock];
		}
	}

	// Write out the rest of the data as full blocks, ignoring anything that might already be in the sectors already
	if (len > 0)
	{
		// Find as many new blocks required to hold the requested data size
		std::vector<uint32_t> blockList;
		// TODO: Why does this break when specifying the first block?
		if (get_available_block_offsets(len, SEARCH_MODE_ANY, SEARCH_START_NONE, blockList) < 0)
			return -1;

		if (filedesc.curblock == BLOCK_END || filedesc.curblock == 0)
		{
			filedesc.curblock = filedesc.entry.offset & 0xffffff;
			while (m_partition.blocks[filedesc.curblock] != BLOCK_END && m_partition.blocks[filedesc.curblock] != 0)
				filedesc.curblock = m_partition.blocks[filedesc.curblock];
		}
		// Update blocks table
		if (blockList.size() > 0)
		{
			uint32_t offs = filedesc.curblock;
			for (int i = 0; i < blockList.size(); i++)
			{
				m_partition.blocks[offs] = blockList[i];
				offs = blockList[i];
			}

			m_partition.blocks[offs] = BLOCK_END;
			filedesc.curblock = blockList[0];
		}

		while (len > 0)
		{
			const auto write_len = std::min<size_t>(
				m_partition.blockSize,
				len
			);

			std::fill_n(block, m_partition.blockSize, 0);
			memcpy(block, data + written_len, write_len);

			const auto offs = get_raw_offset_from_block_offset(filedesc.curblock);

			size_t written = 0;
			write_some_at(offs, block, write_len, written);

			len -= written;
			written_len += written;
			filedesc.block_suboffset += written;
			filedesc.block_offset += written;

			if (filedesc.block_suboffset >= m_partition.blockSize)
			{
				filedesc.block_suboffset %= m_partition.blockSize;
				filedesc.curblock = m_partition.blocks[filedesc.curblock];
			}
		}
	}

	filedesc.entry.size += written_len;

	if (block != nullptr)
	{
		free(block);
		block = nullptr;
	}

	return written_len;
}

int konami_573_network_pcb_unit_storage_pythonfs::dopen(const char *path)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	const uint32_t fd = get_next_fd();

	m_filedescs[fd] = file_desc_t{
		.fd = fd,
		.is_open = true,
		.is_dir = true,
		.block_offset = 0,
		.directory = m_partition.directory,
	};

	if (chdir(path, m_filedescs[fd].directory) < 0)
	{
		m_filedescs.erase(fd);
		return -1;
	}

	return fd;
}

int konami_573_network_pcb_unit_storage_pythonfs::dclose(int fd)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	return close(fd, false);
}

int konami_573_network_pcb_unit_storage_pythonfs::dread(int fd, uint8_t *outbuf)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	if (!is_valid_fd(fd))
		return -2;

	auto &filedesc = m_filedescs[fd];

	if (filedesc.block_offset >= filedesc.directory.size())
		return -1;

	dirent_t *out = (dirent_t*)outbuf;
	memset(out, 0, sizeof(dirent_t));

	if ((filedesc.directory[filedesc.block_offset].offset & ATTRIB_IS_FOLDER) == ATTRIB_IS_FOLDER)
	{
		directory_t dir = {};
		size_t bytes_read;

		auto directory_block_sector = filedesc.directory[filedesc.block_offset].offset & 0xffffff;

		while (true)
		{
			for (int i = 0; i < m_partition.blockSize / sizeof(directory_t); i++)
			{
				read_some_at(
					get_raw_offset_from_block_offset(directory_block_sector) + (i * sizeof(directory_t)),
					&dir,
					sizeof(directory_t),
					bytes_read
				);

				if (bytes_read != sizeof(directory_t) || strlen(dir.name) == 0)
					break;

				if (strcmp(dir.name, ".") == 0)
				{
					filedesc.directory[filedesc.block_offset].size = big_endianize_int32(dir.size);
					break;
				}
			}

			directory_block_sector = m_partition.blocks[directory_block_sector];
			if (directory_block_sector == BLOCK_END || directory_block_sector == 0)
				break;
		}
	}

	strcpy(out->name, filedesc.directory[filedesc.block_offset].name);
	out->stat.size = filedesc.directory[filedesc.block_offset].size;

	filedesc.block_offset++;

	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::getstat(const char *path, uint8_t *outbuf)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	stat_t *out = (stat_t*)outbuf;
	memset(out, 0, sizeof(stat_t));

	int fd = open(path, 0, 0);

	if (!is_valid_fd(fd))
		return -2;

	auto &filedesc = m_filedescs[fd];
	out->size = filedesc.entry.size;
	out->attr = BIT(filedesc.entry.offset, 24, 8);

	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::devctl(uint32_t reqtype, uint32_t param2, uint32_t resplen, uint8_t *data)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	if (reqtype == 2)
	{
		// Disk/partition information?
		uint32_t *output = (uint32_t*)data;

		output[0] = m_partition.header.partitionTotalSectorCount;
		output[1] = m_partition.header.partitionCount1;
		output[2] = m_partition.header.partitionCount2;
		output[3] = m_partition.header.nodeBlockTableSectorCount >> 5;
	}
	else
	{
		LOGFS("devctl unknown type: %02x %04x %04x\n", reqtype, param2, resplen);
		return -1;
	}

	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::format(const char *path, uint32_t start_lba, uint32_t partition_count1, uint32_t partition_count2, uint32_t param4, uint32_t param5)
{
	if (m_dev == nullptr || !m_dev->exists())
		return -1;

	const char *sizestr = path;
	while (*sizestr != '\0' && *sizestr != ',')
		sizestr++;
	while (*sizestr != '\0' && *sizestr == ',')
		sizestr++;
	while (*sizestr != '\0' && !std::isdigit(*sizestr))
		sizestr++;

	auto size = atoi(sizestr);
	while (*sizestr != '\0' && std::isdigit(*sizestr))
		sizestr++;

	uint32_t drive_size = 0;
	if (*sizestr == 'k')
		drive_size = size << 1;
	else if (*sizestr == 'M')
		drive_size = size << 11;
	else if (*sizestr == 'G')
		drive_size = size << 21;

	uint8_t blockSizeMult = 0;
	for (int i = 0; (param5 >> i) > 0x200; i++)
		blockSizeMult++;
	blockSizeMult &= 0x1f;

	const uint32_t sectorsPerPartition = (drive_size - start_lba) / (partition_count1 + partition_count2);

	const uint32_t nodeBlockTableSectorCount = ((sectorsPerPartition >> blockSizeMult) * 4 + 0x1ff) >> 9;
	const uint32_t dataSectorOffset = nodeBlockTableSectorCount * param4 + 1;
	const uint32_t unk1 = ((sectorsPerPartition - dataSectorOffset) >> blockSizeMult)
		- ((((((nodeBlockTableSectorCount + 7) >> 3) + 0x202) >> 9) + (1 << blockSizeMult) - 1) >> blockSizeMult);

	uint8_t headerSector[SECTOR_SIZE];
	std::fill(std::begin(headerSector), std::end(headerSector), 0);

	partition_header_t *header = (partition_header_t*)headerSector;
	std::fill(std::begin(header->padding), std::end(header->padding), 0);
	memcpy(header->magic, "PythonFS", 8);
	memcpy(header->magic2, "\xaf\x86\x8b\x97\x90\x91\xb9\xac", 8);
	header->partitionCount1 = uint8_t(partition_count1);
	header->partitionCount2 = uint8_t(partition_count2);
	header->blockSizeMult = blockSizeMult;
	header->nodeBlockTableCount = uint8_t(param4);
	header->partitionTotalSectorCount = big_endianize_int32(sectorsPerPartition);
	header->nodeBlockTableSectorCount = big_endianize_int32(nodeBlockTableSectorCount);
	header->dataSectorOffset = big_endianize_int32(dataSectorOffset);
	header->unk1 = big_endianize_int32(unk1);

	const auto blocksCount = nodeBlockTableSectorCount * SECTOR_SIZE / 4;
	uint32_t *blocks = (uint32_t*)calloc(((((blocksCount * 4) / SECTOR_SIZE) + 1) * SECTOR_SIZE) / 4, sizeof(uint32_t));
	std::fill_n(blocks, blocksCount, 0);
	blocks[0] = 0;
	blocks[1] = big_endianize_int32(0xffffff); // for the first directory

	uint8_t directorySector[SECTOR_SIZE];
	std::fill(std::begin(directorySector), std::end(directorySector), 0);

	directory_t *directory = (directory_t*)directorySector;
	strcpy(directory->name, ".");
	directory->size = big_endianize_int32(sizeof(directory_t) * 2);
	directory->unk = big_endianize_int32(0);
	directory->offset = big_endianize_int32(1 | ATTRIB_IS_FOLDER);

	directory++;
	strcpy(directory->name, "..");
	directory->size = 0;
	directory->unk = big_endianize_int32(0);
	directory->offset = big_endianize_int32(1 | ATTRIB_IS_FOLDER);

	uint32_t curLba = start_lba;
	for (int i = 0; i < partition_count1 + partition_count2; i++)
	{
		m_dev->write(curLba, headerSector);

		for (int j = 0; j < nodeBlockTableSectorCount; j++)
			m_dev->write(curLba + 1 + j, &blocks[j * (SECTOR_SIZE / 4)]);

		m_dev->write(curLba + dataSectorOffset, directorySector);

		curLba += sectorsPerPartition;
	}

	return parse_filesystem(start_lba);
}

int konami_573_network_pcb_unit_storage_pythonfs::get_available_block_offsets(size_t size, int mode, uint32_t start, std::vector<uint32_t> &blockList)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	const auto blocks = (size / m_partition.blockSize) + ((size % m_partition.blockSize) > 0 ? 1 : 0);
	const auto maxCount = m_partition.header.nodeBlockTableSectorCount * SECTOR_SIZE / 4;

	blockList.clear();

	const bool is_valid_start_addr = start != SEARCH_START_NONE && start != 0 && start != BLOCK_END;

	if (is_valid_start_addr)
		blockList.push_back(start);

	int idx = is_valid_start_addr ? start : 1;
	while (idx < maxCount && blockList.size() < blocks)
	{
		int free = 0;

		if (is_valid_start_addr && idx == start && m_partition.blocks[idx] == BLOCK_END)
		{
			free++;
			idx++;
		}

		if (mode == SEARCH_MODE_CONTIGUOUS || mode == SEARCH_MODE_ANY)
		{
			for (int j = idx; j < maxCount; j++)
			{
				if (m_partition.blocks[j] != 0)
					break;

				free++;
			}

			if (free >= blocks)
			{
				blockList.clear();

				for (int block = 0; block < blocks; block++)
					blockList.push_back(idx + block);

				break;
			}
		}

		if (mode == SEARCH_MODE_ANY)
		{
			if (m_partition.blocks[idx] == 0)
				blockList.push_back(idx);
		}

		idx++;
	}

	return blockList.size() > 0 ? 0 : -1;
}

uint32_t konami_573_network_pcb_unit_storage_pythonfs::get_raw_offset_from_block_offset(uint32_t offset)
{
	return m_partition.offset + (m_partition.header.dataSectorOffset * SECTOR_SIZE) + ((offset - 1) * m_partition.blockSize);
}

uint32_t konami_573_network_pcb_unit_storage_pythonfs::get_lba_from_block_offset(uint32_t offset)
{
	return get_raw_offset_from_block_offset(offset) / SECTOR_SIZE;
}

uint32_t konami_573_network_pcb_unit_storage_pythonfs::get_lba_from_file_offset(uint32_t block_offset, uint32_t offset)
{
	while (m_partition.blocks[block_offset] != BLOCK_END)
		block_offset = m_partition.blocks[block_offset];

	return (get_raw_offset_from_block_offset(m_partition.blocks[block_offset]) + offset) / SECTOR_SIZE;
}

int konami_573_network_pcb_unit_storage_pythonfs::write_directories(std::vector<directory_t> dir, std::vector<uint32_t> &blockList)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	if (blockList.size() == 0)
		return dir.size() > 0 ? -1 : 0;

	// Deallocate any blocks that may not need allocating anymore
	uint32_t offs = blockList[0];
	while (offs != BLOCK_END && offs != 0)
	{
		auto cur = offs;
		offs = m_partition.blocks[offs];
		m_partition.blocks[cur] = 0;
	}

	// Update blocks table
	offs = blockList[0];
	for (int i = 1; i < blockList.size(); i++)
	{
		m_partition.blocks[offs] = blockList[i];
		offs = blockList[i];
	}
	m_partition.blocks[offs] = BLOCK_END;

	int parsed_idx = 0;
	for (auto offset : blockList)
	{
		for (int j = 0; j < m_partition.blockSize / SECTOR_SIZE; j++)
		{
			uint8_t sector[SECTOR_SIZE];
			directory_t *output = (directory_t*)sector;

			std::fill(std::begin(sector), std::end(sector), 0);

			for (int i = 0; i < SECTOR_SIZE / sizeof(directory_t) && parsed_idx < dir.size(); i++)
			{
				strcpy(output->name, dir[parsed_idx].name);
				output->size = big_endianize_int32(dir[parsed_idx].size);
				output->unk = big_endianize_int32(dir[parsed_idx].unk);
				output->offset = big_endianize_int32(dir[parsed_idx].offset);

				output++;
				parsed_idx++;
			}

			m_dev->write(get_lba_from_block_offset(offset) + j, sector);
		}
	}

	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::add_new_entry_to_directory(const char *path, directory_t &entry)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	std::vector<directory_t> curdir;
	if (chdir(path, curdir) < 0)
		return -1;

	directory_t thisdir;
	bool found = false;
	for (auto &c : curdir)
	{
		if (strcmp(c.name, ".") == 0)
		{
			thisdir = c;
			found = true;
			break;
		}
	}

	if (!found || strcmp(thisdir.name, ".") != 0)
		return -1;

	found = false;
	for (int i = 0; i < curdir.size(); i++)
	{
		if (strcmp(curdir[i].name, entry.name) == 0)
		{
			curdir[i] = entry;
			found = true;
			break;
		}
	}

	if (!found)
		curdir.push_back(entry);

	for (auto &c : curdir)
	{
		if (strcmp(c.name, ".") == 0)
		{
			c.size = curdir.size() * sizeof(directory_t);
			break;
		}
	}

	// Can fit into this block?
	const auto cursize = curdir.size() * sizeof(directory_t);
	if (cursize + sizeof(directory_t) >= m_partition.blockSize)
	{
		// TODO: Write out the new blocks
		fatalerror("TODO: Write out the new blocks\n");
	}
	else
	{
		std::vector<uint32_t> curdirBlocks;

		uint32_t curoffs = thisdir.offset & 0xffffff;
		uint32_t lastoffs = 0xffffffff;
		while (true)
		{
			if (curoffs == BLOCK_END || curoffs == lastoffs)
				break;

			curdirBlocks.push_back(curoffs);
			lastoffs = curoffs;
			curoffs = m_partition.blocks[curoffs];
		}

		write_directories(curdir, curdirBlocks);
	}

	write_block_table();

	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::remove_entry_from_directory(const char *path, const char *name)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	std::vector<directory_t> curdir;
	if (chdir(path, curdir) < 0)
		return -1;

	directory_t thisdir;
	bool found = false;
	for (auto &c : curdir)
	{
		if (strcmp(c.name, ".") == 0)
		{
			thisdir = c;
			found = true;
			break;
		}
	}

	if (!found || strcmp(thisdir.name, ".") != 0)
		return -1;

	found = false;
	for (int i = 0; i < curdir.size(); i++)
	{
		if (strcmp(curdir[i].name, name) == 0)
		{
			curdir.erase(curdir.begin() + i);
			found = true;
			break;
		}
	}

	if (!found)
		return -1;

	for (auto &c : curdir)
	{
		if (strcmp(c.name, ".") == 0)
		{
			c.size = curdir.size() * sizeof(directory_t);
			break;
		}
}
	std::vector<uint32_t> curdirBlocks;
	uint32_t curoffs = thisdir.offset & 0xffffff;
	uint32_t lastoffs = 0xffffffff;
	while (true)
	{
		if (curoffs == BLOCK_END || curoffs == lastoffs)
			break;

		curdirBlocks.push_back(curoffs);
		lastoffs = curoffs;
		curoffs = m_partition.blocks[curoffs];
	}

	write_directories(curdir, curdirBlocks);

	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::mkdir_internal(const char *path, const char *folder)
{
	if (!m_dev || !m_dev->exists())
		return -10;

	std::vector<directory_t> curdir;
	if (chdir(path, curdir) < 0)
		return -11;

	// Don't make a folder again if it already exists
	for (auto &c : curdir)
	{
		if (strcmp(c.name, folder) == 0)
			return 0;
	}

	directory_t thisdir;
	bool found = false;
	for (auto &c : curdir)
	{
		if (strcmp(c.name, ".") == 0)
		{
			thisdir = c;
			found = true;
			break;
		}
	}

	if (!found || strcmp(thisdir.name, ".") != 0)
		return -12;

	// Find offset to place new folder entry at
	std::vector<uint32_t> availableBlocksList;
	if (get_available_block_offsets(sizeof(directory_t) * 2, SEARCH_MODE_ANY, SEARCH_START_NONE, availableBlocksList) < 0)
		return -13;

	std::vector<directory_t> newdir;
	newdir.push_back(directory_t{
		.name = ".",
		.size = sizeof(directory_t) * 2,
		.unk = 0,
		.offset = uint32_t(availableBlocksList[0] & 0xffffff) | ATTRIB_IS_FOLDER,
	});
	newdir.push_back(directory_t{
		.name = "..",
		.size = 0,
		.unk = 0,
		.offset = (thisdir.offset & 0xffffff) | ATTRIB_IS_FOLDER,
	});

	write_directories(newdir, availableBlocksList);

	auto newdirentry = directory_t{
			.size = 0,
			.unk = 0x1dfe200,
			.offset = uint32_t(availableBlocksList[0] & 0xffffff) | ATTRIB_IS_FOLDER,
	};
	strcpy(newdirentry.name, folder);
	add_new_entry_to_directory(path, newdirentry);

	return 0;
}

void konami_573_network_pcb_unit_storage_pythonfs::write_block_table()
{
	if (!m_dev || !m_dev->exists())
		return;

	uint8_t sector[SECTOR_SIZE];
	uint32_t block_idx = 0;

	for (int i = 0; i < m_partition.header.nodeBlockTableSectorCount; i++)
	{
		std::fill(std::begin(sector), std::end(sector), 0);

		uint32_t *output = (uint32_t*)sector;

		for (int j = 0; j < SECTOR_SIZE / 4; j++)
			output[j] = big_endianize_int32(m_partition.blocks[block_idx++]);

		m_dev->write(m_partition.offset / SECTOR_SIZE + (1 + i), sector);
	}
}

int konami_573_network_pcb_unit_storage_pythonfs::mkdir(const char *path, int mode)
{
	if (!m_dev || !m_dev->exists())
		return -21;

	// check if path already exists
	if (chdir(path) >= 0)
		return 0;

	int pathoffs = 0;

	if (path[0] == '/')
	{
		chdir("/");
		while(pathoffs < strlen(path) && path[pathoffs] == '/')
			pathoffs++;
	}

	// make all folders in path as needed
	int lastpathoffs = pathoffs;
	while (pathoffs < strlen(path))
	{
		while(pathoffs < strlen(path) && path[pathoffs] != '/')
			pathoffs++;

		if (pathoffs == lastpathoffs)
			break;

		auto folder = std::string(path, pathoffs);
		std::vector<directory_t> ignore;
		if (chdir(folder.c_str(), ignore) < 0)
		{
			int r;

			// make new directory
			if ((r = mkdir_internal(std::string(path, lastpathoffs).c_str(), std::string(path + lastpathoffs, pathoffs - lastpathoffs).c_str())) < 0)
				return r;
		}

		// If we still can't chdir into the directory then something is broken
		ignore.clear();
		if (chdir(folder.c_str(), ignore) < 0)
			return -22;

		while(pathoffs < strlen(path) && path[pathoffs] == '/')
			pathoffs++;

		lastpathoffs = pathoffs;
	}

	return 0;
}

int konami_573_network_pcb_unit_storage_pythonfs::rmdir(const char *path)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	std::vector<directory_t> entries;
	if (chdir(path, entries) < 0)
		return -1;

	bool has_files = false;
	for (auto c : entries)
	{
		if (strcmp(c.name, "..") != 0 && strcmp(c.name, ".") != 0)
		{
			has_files = true;
			break;
		}
	}

	if (has_files)
		return -1;

	// Get upper path
	char *pathptr = (char*)path;
	size_t pathoffs = 0;

	while (strlen(pathptr) > 0 && (pathptr = strstr((char*)pathptr, "/")) != nullptr)
	{
		pathptr++;
		pathoffs = pathptr - path;
	}

	std::string curpath = m_partition.directory_path;
	if (pathoffs > 0)
	{
		if (path[0] == '/')
			curpath = std::string(path, pathoffs);
		else
			curpath += std::string(path, pathoffs);
	}

	return remove_entry_from_directory(curpath.c_str(), path + pathoffs);
}

int konami_573_network_pcb_unit_storage_pythonfs::remove(const char *path)
{
	if (!m_dev || !m_dev->exists())
		return -1;

	m_dev->dump();

	// chdir to path if needed
	char *pathptr = (char*)path;
	size_t pathoffs = 0;

	while (strlen(pathptr) > 0 && (pathptr = strstr((char*)pathptr, "/")) != nullptr)
	{
		pathptr++;
		pathoffs = pathptr - path;
	}

	std::string curpath = m_partition.directory_path;
	if (pathoffs > 0)
	{
		if (path[0] == '/')
			curpath = std::string(path, pathoffs);
		else
			curpath += std::string(path, pathoffs);
	}

	// Can't remove these?
	if (strcmp(path + pathoffs, "..") == 0 || strcmp(path + pathoffs, ".") == 0)
		return 0;

	std::vector<directory_t> entries;
	if (chdir(curpath.c_str(), entries) < 0)
		return -1;

	for (auto c : entries)
	{
		if (strcmp(c.name, path + pathoffs) == 0)
		{
			// Deallocate blocks used by file
			auto offs = c.offset & 0xffffff;
			while (offs != BLOCK_END && offs != 0)
			{
				auto cur = offs;
				offs = m_partition.blocks[cur];
				m_partition.blocks[cur] = 0;
			}

			write_block_table();

			// TODO: Clean up sectors to make HDD image compress better?
			break;
		}
	}

	return remove_entry_from_directory(curpath.c_str(), path + pathoffs);
}
