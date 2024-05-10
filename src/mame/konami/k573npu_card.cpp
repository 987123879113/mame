// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
TODO: Clean up sockets and pointers as needed?
TODO: Implement a proper way to lock the output buffer in a thread safe manner
TODO: There's probably a lot of endianness issues, but the code works on x64 and ARM so not a big deal
TODO: Custom DNS would be nice



Note: After installing game and enabling e-amusement, immediately restart the game before attempting network connections
or else the game will be trying to use its default network settings instead of pulling network settings from the NPU.



How to prepare CHD HDD image:
chdman createhd --sectorsize 512 --size 10056130560 -o filename.chd


How to configure internet settings:
Make a new file named k573npu.ini and put it in the same folder as mame.exe with the following format:

# 16 character hex string (0-9a-fA-F)
# This value also acts as the PCBID sent to server for machine identification.
# You can scramble your PCBID to match the NPU ID format as shown below (aa, bb, cc, ... groups are 2 hex characters)
# PCBID format: 0140aaggffeeddccbbhh
# NPU ID format: aabbccddeeffgghh
npu_id          1234567890abcdef

# MAC address of the network PCB unit
mac_address     12:34:56:78:9a:bc

# IP address of the network PCB unit
ip_address      10.1.1.24

# Point to whatever server has the service URL information configured
dns_server1     10.1.1.1
dns_server2     10.1.1.1

# The service URL will become service.<domain_name>
domain_name     ""

# NTP time server, must be able to respond to ICMP pings
# Provided default: pool.ntp.org
ntp_server      162.159.200.123

# If you don't know then just copy what is in your network adapter settings (or look at ipconfig/ifconfig output)
subnet_mask     255.255.255.0
default_gateway 10.1.1.1
dhcp_server     10.1.1.1

*/
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

#ifdef _WIN32
#ifndef _WIN32_WINNT
	#define _WIN32_WINNT 0x0600
#endif

#include <winsock2.h>
#include <winsock.h>
#include <Ws2tcpip.h>

#ifndef sa_family_t
	typedef ADDRESS_FAMILY sa_family_t;
#endif

#ifndef in_port_t
	typedef USHORT in_port_t;
#endif

#ifndef in_addr_t
	typedef ULONG in_addr_t;
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

#define LOG_REG (1U << 1)
#define LOG_MEM_VERBOSE (1U << 2)
#define LOG_CMD (1U << 3)
#define LOG_PACKET (1U << 4)
#define LOG_NET (1U << 5)
#define LOG_FS (1U << 6)

// #define VERBOSE (LOG_GENERAL | LOG_REG | LOG_CMD | LOG_NET | LOG_FS | LOG_PACKET /*| LOG_MEM_VERBOSE*/)
#define LOG_OUTPUT_FUNC osd_printf_info

#include "logmacro.h"

#define LOGREG(...)        LOGMASKED(LOG_REG, __VA_ARGS__)
#define LOGMEMVERBOSE(...) LOGMASKED(LOG_MEM_VERBOSE, __VA_ARGS__)
#define LOGCMD(...)        LOGMASKED(LOG_CMD, __VA_ARGS__)
#define LOGNET(...)        LOGMASKED(LOG_NET, __VA_ARGS__)
#define LOGFS(...)         LOGMASKED(LOG_FS, __VA_ARGS__)
#define LOGPACKET(...)     LOGMASKED(LOG_PACKET, __VA_ARGS__)


enum
{
	// These are just to make it easier to see where things failed. Not used on real hardware
	NOT_VALID_FD = -2,
	SEEK_OUT_OF_RANGE = -3,
};


konami_573_network_pcb_unit_hdd::konami_573_network_pcb_unit_hdd(harddisk_image_device *hdd)
	: m_hdd(hdd)
{
	m_partition = {};
	m_next_fd = 0;
	m_filedescs.clear();
}

void konami_573_network_pcb_unit_hdd::reset()
{
	if (m_hdd && m_hdd->exists())
		m_hdd->set_block_size(SECTOR_SIZE);

	m_partition = {};
	m_next_fd = 0;
	m_mount_size_sectors = 0;
	m_filedescs.clear();

	m_hdd_offset = 0;
}

void konami_573_network_pcb_unit_hdd::write_some_at(std::uint64_t offset, void *buffer, std::size_t length, std::size_t &actual)
{
	if (!m_hdd || !m_hdd->exists())
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

		m_hdd->read(offset / SECTOR_SIZE, sector);
		memcpy(sector + (offset % SECTOR_SIZE), &((uint8_t*)buffer)[actual], size);

		m_hdd->write(offset / SECTOR_SIZE, sector);

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

		m_hdd->write(offset / SECTOR_SIZE, sector);

		actual += size;
		offset += size;
	}
}

void konami_573_network_pcb_unit_hdd::read_some_at(std::uint64_t offset, void *buffer, std::size_t length, std::size_t &actual)
{
	if (!m_hdd || !m_hdd->exists())
		return;

	uint8_t sector[SECTOR_SIZE];

	actual = 0;

	while (actual < length)
	{
		const auto size = std::min<size_t>(SECTOR_SIZE - (offset % SECTOR_SIZE), length - actual);
		m_hdd->read(offset / SECTOR_SIZE, sector);
		memcpy(&((uint8_t*)buffer)[actual], &sector[offset % SECTOR_SIZE], size);
		actual += size;
		offset += size;
	}
}

void konami_573_network_pcb_unit_hdd::read_some(void *buffer, std::size_t length, std::size_t &actual)
{
	if (!m_hdd || !m_hdd->exists())
		return;

	uint8_t sector[SECTOR_SIZE];

	actual = 0;

	while (actual < length)
	{
		const auto size = std::min<size_t>(SECTOR_SIZE - (m_hdd_offset % SECTOR_SIZE), length - actual);
		m_hdd->read(m_hdd_offset / SECTOR_SIZE, sector);
		memcpy(&((uint8_t*)buffer)[actual], &sector[m_hdd_offset % SECTOR_SIZE], size);
		actual += size;
		m_hdd_offset += size;
	}
}

void konami_573_network_pcb_unit_hdd::seek(std::int64_t offset, int whence)
{
	if (whence == SEEK_SET)
		m_hdd_offset = offset;
	else if (whence == SEEK_CUR)
		m_hdd_offset += offset;
}

bool konami_573_network_pcb_unit_hdd::is_valid_fd(int fd)
{
	return m_filedescs.find(fd) != m_filedescs.end() && m_filedescs[fd].is_open;
}

uint32_t konami_573_network_pcb_unit_hdd::get_next_fd()
{
	uint32_t fd = m_next_fd;

	while (m_filedescs.find(fd) != m_filedescs.end())
		fd++;

	m_next_fd = fd + 1;

	return fd;
}

int konami_573_network_pcb_unit_hdd::parse_filesystem()
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

	// TODO: This only currently handles one partition chunk, but there can be multiple.
	// Never seen it use multiple partitions so not sure how it's handled yet.
	uint64_t next_partition = START_SECTOR * SECTOR_SIZE;
	size_t bytes_read = 0;

	if (m_partition.blocks != nullptr)
		free(m_partition.blocks);

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

int konami_573_network_pcb_unit_hdd::mount(const char *path)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

	int idx = 0;

	while (idx < strlen(path) && path[idx] != ',')
		idx++;
	while (idx < strlen(path) && !std::isdigit(path[idx]))
		idx++;

	const int size = atoi(path + idx);

	while (idx < strlen(path) && !std::isalpha(path[idx]))
		idx++;

	const char m = path[idx];

	uint32_t bytes = 0;
	if (m == 'k')
		bytes = size << 1;
	else if (m == 'M')
		bytes = size << 11;
	else if (m == 'G')
		bytes = size << 21;

	m_mount_size_sectors = bytes;

	return parse_filesystem();
}

int konami_573_network_pcb_unit_hdd::umount(const char *path)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

	// TODO: Clean up mounted partition state
	return 0;
}

int konami_573_network_pcb_unit_hdd::open(const char *path, uint32_t flags, uint32_t mode)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

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

int konami_573_network_pcb_unit_hdd::close(int fd, bool allow_write)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	if (!is_valid_fd(fd))
		return 0;

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

int konami_573_network_pcb_unit_hdd::lseek(int fd, uint32_t offset, int whence)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -5;

	if (!is_valid_fd(fd))
		return NOT_VALID_FD;

	auto &filedesc = m_filedescs[fd];

	if (whence == SEEK_SET)
	{
		if (offset > filedesc.entry.size)
			return SEEK_OUT_OF_RANGE;

		filedesc.block_offset = offset;
	}
	else if (whence == SEEK_CUR)
	{
		if (offset + filedesc.block_offset > filedesc.entry.size)
			return SEEK_OUT_OF_RANGE;

		filedesc.block_offset += offset;
	}
	else if (whence == SEEK_END)
	{
		// TODO: This probably has an off by 1 issue, untested
		if (offset > filedesc.entry.size)
			return SEEK_OUT_OF_RANGE;

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

int konami_573_network_pcb_unit_hdd::chdir(const char *path)
{
	if (!m_hdd || !m_hdd->exists())
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

int konami_573_network_pcb_unit_hdd::chdir(const char *path, std::vector<directory_t> &output)
{
	if (!m_hdd || !m_hdd->exists())
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

int konami_573_network_pcb_unit_hdd::read(int fd, uint32_t len, uint8_t *outbuf, uint32_t &read_count)
{
	read_count = 0;

	if (!m_hdd || !m_hdd->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	if (!is_valid_fd(fd))
		return NOT_VALID_FD;

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

int konami_573_network_pcb_unit_hdd::write(int fd, uint32_t len, uint8_t *data)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	if (!is_valid_fd(fd))
		return NOT_VALID_FD;

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
		free(block);

	return written_len;
}

int konami_573_network_pcb_unit_hdd::dopen(const char *path)
{
	if (!m_hdd || !m_hdd->exists())
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

int konami_573_network_pcb_unit_hdd::dclose(int fd)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

	return close(fd, false);
}

int konami_573_network_pcb_unit_hdd::dread(int fd, uint8_t *outbuf)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	if (!is_valid_fd(fd))
		return NOT_VALID_FD;

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

int konami_573_network_pcb_unit_hdd::getstat(const char *path, uint8_t *outbuf)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

	if (!m_partition.is_loaded)
		return -1;

	stat_t *out = (stat_t*)outbuf;
	memset(out, 0, sizeof(stat_t));

	int fd = open(path, 0, 0);

	if (!is_valid_fd(fd))
		return NOT_VALID_FD;

	auto &filedesc = m_filedescs[fd];
	out->size = filedesc.entry.size;
	out->attr = BIT(filedesc.entry.offset, 24, 8);

	return 0;
}

int konami_573_network_pcb_unit_hdd::devctl(uint32_t reqtype, uint32_t param2, uint32_t resplen, uint8_t *data)
{
	if (!m_hdd || !m_hdd->exists())
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
		LOGFS("devctl unknwon type: %02x %04x %04x\n", reqtype, param2, resplen);
		return -1;
	}

	return 0;
}

int konami_573_network_pcb_unit_hdd::format(uint32_t startLba, uint32_t partitionCount1, uint32_t partitionCount2, uint32_t param4, uint32_t param5)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

	uint8_t blockSizeMult = 0;
	for (int i = 0; (param5 >> i) > 0x200; i++)
		blockSizeMult++;
	blockSizeMult &= 0x1f;

	const uint32_t sectorsPerPartition = (m_mount_size_sectors - startLba) / (partitionCount1 + partitionCount2);
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
	header->partitionCount1 = uint8_t(partitionCount1);
	header->partitionCount2 = uint8_t(partitionCount2);
	header->blockSizeMult = blockSizeMult;
	header->nodeBlockTableCount = uint8_t(param4);
	header->partitionTotalSectorCount = big_endianize_int32(sectorsPerPartition);
	header->nodeBlockTableSectorCount = big_endianize_int32(nodeBlockTableSectorCount);
	header->dataSectorOffset = big_endianize_int32(dataSectorOffset);
	header->unk1 = big_endianize_int32(unk1);

	const auto blocksCount = nodeBlockTableSectorCount * SECTOR_SIZE / 4;
	uint32_t *blocks = (uint32_t*)calloc(blocksCount, sizeof(uint32_t));
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

	uint32_t curLba = startLba;
	for (int i = 0; i < partitionCount1 + partitionCount2; i++)
	{
		m_hdd->write(curLba, headerSector);

		for (int j = 0; j < nodeBlockTableSectorCount; j++)
			m_hdd->write(curLba + 1 + j, &blocks[j * (SECTOR_SIZE / 4)]);

		m_hdd->write(curLba + dataSectorOffset, directorySector);

		curLba += sectorsPerPartition;
	}

	parse_filesystem();

	return 0;
}

int konami_573_network_pcb_unit_hdd::get_available_block_offsets(size_t size, int mode, uint32_t start, std::vector<uint32_t> &blockList)
{
	if (!m_hdd || !m_hdd->exists())
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

uint32_t konami_573_network_pcb_unit_hdd::get_raw_offset_from_block_offset(uint32_t offset)
{
	return m_partition.offset + (m_partition.header.dataSectorOffset * SECTOR_SIZE) + ((offset - 1) * m_partition.blockSize);
}

uint32_t konami_573_network_pcb_unit_hdd::get_lba_from_block_offset(uint32_t offset)
{
	return get_raw_offset_from_block_offset(offset) / SECTOR_SIZE;
}

uint32_t konami_573_network_pcb_unit_hdd::get_lba_from_file_offset(uint32_t block_offset, uint32_t offset)
{
	while (m_partition.blocks[block_offset] != BLOCK_END)
		block_offset = m_partition.blocks[block_offset];

	return (get_raw_offset_from_block_offset(m_partition.blocks[block_offset]) + offset) / SECTOR_SIZE;
}

int konami_573_network_pcb_unit_hdd::write_directories(std::vector<directory_t> dir, std::vector<uint32_t> &blockList)
{
	if (!m_hdd || !m_hdd->exists())
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

			m_hdd->write(get_lba_from_block_offset(offset) + j, sector);
		}
	}

	return 0;
}

int konami_573_network_pcb_unit_hdd::add_new_entry_to_directory(const char *path, directory_t &entry)
{
	if (!m_hdd || !m_hdd->exists())
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

int konami_573_network_pcb_unit_hdd::remove_entry_from_directory(const char *path, const char *name)
{
	if (!m_hdd || !m_hdd->exists())
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

int konami_573_network_pcb_unit_hdd::mkdir_internal(const char *path, const char *folder)
{
	if (!m_hdd || !m_hdd->exists())
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

void konami_573_network_pcb_unit_hdd::write_block_table()
{
	if (!m_hdd || !m_hdd->exists())
		return;

	uint8_t sector[SECTOR_SIZE];
	uint32_t block_idx = 0;

	for (int i = 0; i < m_partition.header.nodeBlockTableSectorCount; i++)
	{
		std::fill(std::begin(sector), std::end(sector), 0);

		uint32_t *output = (uint32_t*)sector;

		for (int j = 0; j < SECTOR_SIZE / 4; j++)
			output[j] = big_endianize_int32(m_partition.blocks[block_idx++]);

		m_hdd->write(m_partition.offset / SECTOR_SIZE + (1 + i), sector);
	}
}

int konami_573_network_pcb_unit_hdd::mkdir(const char *path, int mode)
{
	if (!m_hdd || !m_hdd->exists())
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

int konami_573_network_pcb_unit_hdd::rmdir(const char *path)
{
	if (!m_hdd || !m_hdd->exists())
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

int konami_573_network_pcb_unit_hdd::remove(const char *path)
{
	if (!m_hdd || !m_hdd->exists())
		return -1;

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

////////////////////////////////


enum {
	// Used by 0x3a opcode
	REQVAL_ADDR_SUBNET_MASK = 0x01,
	REQVAL_ADDR_DEFAULT_GATEWAY = 0x03,
	REQVAL_ADDR_DNS_SERVERS = 0x06,
	REQVAL_ADDR_DOMAIN_NAME = 0x0f,
	REQVAL_ADDR_NTP_SERVER = 0x2a,
	REQVAL_ADDR_DHCP_SERVER = 0x36,
	REQVAL_ADDR_IP_ADDR = 0x101,
};

enum {
	// System opcodes:
	// 0x00
	// 0x01
	OPCODE_WRITE_MEMORY = 0x80,
	OPCODE_READ_MEMORY = 0x81,
	OPCODE_EXECUTE_MEMORY = 0x82,
	// 0x83

	// Network opcodes:
	// OPCODE_SOCKET_ACCEPT = 0x20,
	// OPCODE_SOCKET_BIND = 0x21,
	OPCODE_SOCKET_CLOSE = 0x22,
	OPCODE_SOCKET_CONNECT = 0x23,
	// OPCODE_SOCKET_GETPEERNAME = 0x24,
	// OPCODE_SOCKET_GETSOCKNAME = 0x25,
	// OPCODE_SOCKET_IOCTLSOCKET = 0x26,
	// OPCODE_SOCKET_LISTEN = 0x27,
	OPCODE_SOCKET_RECV = 0x28,
	OPCODE_SOCKET_RECVFROM = 0x29,
	OPCODE_SOCKET_SEND = 0x2a,
	OPCODE_SOCKET_SENDTO = 0x2b,
	OPCODE_SOCKET_SETSOCKOPT = 0x2c,
	// OPCODE_SOCKET_SHUTDOWN = 0x2d,
	OPCODE_SOCKET_SOCKET = 0x2e,
	// OPCODE_NET_GET_IP_ADDR = 0x2f,
	// OPCODE_NET_SET_IP_ADDR = 0x30,
	// OPCODE_NET_GET_SUBNET = 0x31,
	// OPCODE_NET_SET_SUBNET = 0x32,
	// OPCODE_NET_GET_DEFAULT_GATEWAY = 0x33,
	// OPCODE_NET_SET_DEFAULT_GATEWAY = 0x34,
	// OPCODE_NET_ = 0x35, // Somewhere around here is probably DNS server(s) and NTP server set/get
	// OPCODE_NET_ = 0x36,
	// OPCODE_NET_ = 0x37,
	// OPCODE_NET_ = 0x38,
	// OPCODE_NET_ = 0x39,
	OPCODE_NET_GET_SETTINGS = 0x3a, // naming?

	// Filesystem opcodes:
	// Seems to be loosely based on the PS2's PFS, maybe Sony was involved in some way, or it was backported to Sys573 NPU while working on Python games?
	OPCODE_FILESYSTEM_INITIALIZE = 0x40,
	OPCODE_FILESYSTEM_OPEN = 0x41,
	OPCODE_FILESYSTEM_CLOSE = 0x42,
	OPCODE_FILESYSTEM_READ = 0x43,
	OPCODE_FILESYSTEM_WRITE = 0x44,
	OPCODE_FILESYSTEM_LSEEK = 0x45,
	OPCODE_FILESYSTEM_IOCTL = 0x46,
	OPCODE_FILESYSTEM_DOPEN = 0x47,
	OPCODE_FILESYSTEM_DCLOSE = 0x48,
	OPCODE_FILESYSTEM_DREAD = 0x49,
	OPCODE_FILESYSTEM_REMOVE = 0x4a,
	OPCODE_FILESYSTEM_MKDIR = 0x4b,
	OPCODE_FILESYSTEM_RMDIR = 0x4c,
	OPCODE_FILESYSTEM_GETSTAT = 0x4d,
	OPCODE_FILESYSTEM_CHSTAT = 0x4e,
	OPCODE_FILESYSTEM_RENAME = 0x4f,
	OPCODE_FILESYSTEM_CHDIR = 0x50,
	OPCODE_FILESYSTEM_MOUNT = 0x51,
	OPCODE_FILESYSTEM_UMOUNT = 0x52,
	OPCODE_FILESYSTEM_DEVCTL = 0x53,
	OPCODE_FILESYSTEM_FORMAT = 0x54,
};

enum {
	REG_REQUESTED_STATE = 0x00,
	REG_CURRENT_STATE = 0x02,
	REG_FIFO_READ = 0x20,
	REG_FIFO_WRITE = 0x20,
	REG_FIFO_READ_AVAIL_SIZE = 0x26,
	REG_FIFO_WRITE_AVAIL_SIZE = 0x2c,
	REG_FIFO_READ_OFFSET = 0x34,
	REG_FIFO_WRITE_OFFSET = 0x36,
	REG_FPGA_CHECK_RUNNING = 0x3a,
};


DEFINE_DEVICE_TYPE(KONAMI_573_NETWORK_PCB_UNIT_PCCARD, konami_573_network_pcb_unit_pccard_device, "k573npu_card", "Konami Network PCB Unit PC Card")

konami_573_network_pcb_unit_pccard_device::konami_573_network_pcb_unit_pccard_device(const machine_config &mconfig,  const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, KONAMI_573_NETWORK_PCB_UNIT_PCCARD, tag, owner, clock),
	device_pccard_interface(mconfig, *this),
	m_npu_hdd(*this, "npu_hdd")
{
}

void konami_573_network_pcb_unit_pccard_device::device_start()
{
#ifdef _WIN32
	WSADATA wsa_data;
	WSAStartup(MAKEWORD(1,1), &wsa_data);
#endif

	m_thread = std::thread([this] {
		asio::io_service::work work(m_io_service);
		m_io_service.run();
	});

	std::fill(std::begin(config.npu_id), std::end(config.npu_id), 0);
	std::fill(std::begin(config.mac_address), std::end(config.mac_address), 0);
	std::fill(std::begin(config.ip_address), std::end(config.ip_address), 0);
	std::fill(std::begin(config.subnet_mask), std::end(config.subnet_mask), 0);
	std::fill(std::begin(config.default_gateway), std::end(config.default_gateway), 0);
	std::fill(std::begin(config.dns_server1), std::end(config.dns_server1), 0);
	std::fill(std::begin(config.dns_server2), std::end(config.dns_server2), 0);
	std::fill(std::begin(config.dhcp_server), std::end(config.dhcp_server), 0);
	std::fill(std::begin(config.ntp_server), std::end(config.ntp_server), 0);
	std::fill(std::begin(config.domain_name), std::end(config.domain_name), 0);
	parse_ini_file("k573npu.ini");
}

void konami_573_network_pcb_unit_pccard_device::device_stop()
{
#ifdef _WIN32
	WSACleanup();
#endif

	m_io_service.stop();
	m_thread.join();
}

void konami_573_network_pcb_unit_pccard_device::interface_post_start()
{
	m_hdd = std::make_unique<konami_573_network_pcb_unit_hdd>(m_npu_hdd);
}

void konami_573_network_pcb_unit_pccard_device::device_reset()
{
	m_hdd->reset();

	m_cd1_cb(0);
	m_cd2_cb(0);

	m_state_cur = 0;
	m_val_unk = 0;
	m_init_cur_step = 0;
	m_buffer_write_length = 0;
	m_buffer_read_length = 0;
	m_state_requested = 0;
	m_packet_id = 0;

	m_output_buf.clear();
	m_input_buf.clear();
}

void konami_573_network_pcb_unit_pccard_device::device_add_mconfig(machine_config &config)
{
	HARDDISK(config, m_npu_hdd);
}

uint16_t konami_573_network_pcb_unit_pccard_device::read_memory(offs_t offset, uint16_t mem_mask)
{
	auto r = 0;

	/*
	0x24 = 0x2c = number of bytes written to 0x30???
	(0x06 & 0xfff0) = 0x0e = reg 0x24 & 0xfff0

	0x3c - some offset??
	0x00 - reg | (reg 0x3c & 0xfff0)
	0x02 - reg | (reg 0x3c & 0xfff0)
	0x0e - reg | (reg 0x3c & 0xfff0)
	0x10 - (reg & 0xfff0) | (reg 0x3c & 0xfff0)
	0x1e - reg | (reg 0x3c & 0xfff0)
	0x2c - reg | (reg 0x3c)
	0x2e - reg | (reg 0x3c & 0xfff0) | ((reg 0x3c / 4) * 4)
	0x3e - reg | (reg 0x3c & 0xfff0) | ((reg 0x3c / 4) * 4)

	0x30 - after writing value to 0x30, this returns 1 instead of 0?

	0x22 is the same as 0x32?
	0x34 = 0x36 = 0x16 = 0x18
	0x22 = 0x26 = 0x02 = 0x08 <- decrements when reg 0x20 is read when bit 15 of reg 0x30 is set
	0x04 = 0x08?
	*/

	switch (offset * 2)
	{
		case REG_REQUESTED_STATE:
			r = m_state_requested;
			break;

		case REG_CURRENT_STATE:
			r = m_state_cur;
			break;

		case 0x06:
			r = 5;
			break;

		case REG_FIFO_READ:
			if (!m_output_buf.empty())
				r = m_output_buf.pop();

			if (m_state_requested != 8 && m_state_requested != 0x0f)
				r ^= 0xffff;

			if (m_buffer_read_length)
				m_buffer_read_length--;
			break;

		case 0x24:
		case REG_FIFO_WRITE_AVAIL_SIZE:
			r = 0x100 - (m_input_buf.length_locked() % 0x100); // FIFO available size??
			break;

		case 0x22:
		case REG_FIFO_READ_AVAIL_SIZE:
			r = m_buffer_read_length;
			break;

		case 0x30:
			r = m_buffer_write_length > 0;
			break;

		case 0x32:
			r = 0;//m_fifo_buf.length() > 0;
			break;

		case REG_FIFO_READ_OFFSET:
			r = m_output_buf.tail_locked() & 0xffff;
			break;

		case REG_FIFO_WRITE_OFFSET:
			r = m_input_buf.tail_locked() & 0xffff;
			break;

		case REG_FPGA_CHECK_RUNNING:
			r = 0x5963; // FPGA initialized check
			break;

		case 0x3c:
			r = m_val_unk;
			break;
	}

	if (offset * 2 != REG_FIFO_READ || (offset * 2 == REG_FIFO_READ && m_output_buf.length_locked() < 0x10))
		LOGMEMVERBOSE("%s read_memory %08x %04x %04x\n", machine().describe_context().c_str(), offset * 2, r, mem_mask);

	return r;
}

void konami_573_network_pcb_unit_pccard_device::write_memory(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (offset * 2 != REG_FIFO_WRITE || (offset * 2 == REG_FIFO_WRITE && m_input_buf.length_locked() < 0x10))
		LOGMEMVERBOSE("%s write_memory %08x %04x %04x\n", machine().describe_context().c_str(), offset * 2, data, mem_mask);

	switch (offset * 2)
	{
		case REG_REQUESTED_STATE:
		{
			const auto prev_cmd = m_state_requested;
			m_state_requested = data;

			m_state_cur = data;

			if (m_init_cur_step)
			{
				m_state_cur ^= 0x0f;
				m_init_cur_step--;
			}

			if (data == 2)
			{
				while (m_input_buf.length_locked() > 0)
					m_input_buf.pop();

				while (m_output_buf.length_locked() > 0)
					m_output_buf.pop();

				for (int i = 0; i < 0xa00; i++)
					write_fifo_u16(m_output_buf, i);
			}
			else if (prev_cmd == 8 && data == 0x0f)
			{
				m_output_buf.clear();
				// m_input_buf.clear();

				// 1 must be sent or else the Sys573 thinks the NPU isn't running
				write_fifo_u16(m_output_buf, 1);
				write_fifo_u16(m_output_buf, 0);
				write_fifo_u16(m_output_buf, 0);
			}
			break;
		}

		case 0x04:
			// ???
			m_init_cur_step = data ? 5 : 0;
			break;

		case REG_FIFO_WRITE:
			if (m_buffer_write_length)
			{
				write_fifo_u16(m_input_buf, data);
				m_buffer_write_length--;

				if (!m_buffer_write_length)
				{
					if ((m_state_requested == 8 || m_state_requested == 0x0f)/*x && m_fifo_buf.queue_length() == 0*/)
						process_opcode();

					// m_input_buf.clear();
				}
			}
			break;

		case 0x30:
			// FIFO requested write count?
			if (BIT(data, 15))
			{
				m_buffer_read_length = data & 0x7fff;
			}
			else
			{
				m_buffer_write_length += data;
			}

			break;

		case 0x34:
			// m_input_buf.head(data);
			break;

		case 0x36:
			// m_output_buf.head(data);
			break;

		case 0x3c:
			m_val_unk = data;
			break;
	}
}

uint16_t konami_573_network_pcb_unit_pccard_device::read_reg(offs_t offset, uint16_t mem_mask)
{
	auto r = 0;
	LOGREG("%s read_reg %08x %04x %04x\n", machine().describe_context().c_str(), offset * 2, r, mem_mask);
	return r;
}

void konami_573_network_pcb_unit_pccard_device::write_reg(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGREG("%s write_reg %08x %04x %04x\n", machine().describe_context().c_str(), offset * 2, data, mem_mask);
}

void konami_573_network_pcb_unit_pccard_device::write_fifo_bytes(ringbuffer<uint16_t, 0x10000> &buf, const uint8_t *input, size_t len)
{
	for (int i = 0; i < len; i+=2)
	{
		uint16_t val = input[i];

		if (i + 1 < len)
			val |= input[i+1] << 8;

		buf.push(val);
	}
}

void konami_573_network_pcb_unit_pccard_device::write_fifo_u16(ringbuffer<uint16_t, 0x10000> &buf, uint16_t val)
{
	buf.push(val);
}

void konami_573_network_pcb_unit_pccard_device::write_fifo_u32(ringbuffer<uint16_t, 0x10000> &buf, uint32_t val)
{
	write_fifo_u16(buf, (uint16_t)BIT(val, 0, 16));
	write_fifo_u16(buf, (uint16_t)BIT(val, 16, 16));
}

uint32_t konami_573_network_pcb_unit_pccard_device::read_fifo_u32(ringbuffer<uint16_t, 0x10000> &buf)
{
	return buf.pop() | (buf.pop() << 16);
}

uint16_t konami_573_network_pcb_unit_pccard_device::read_fifo_u16(ringbuffer<uint16_t, 0x10000> &buf)
{
	return buf.pop();
}

size_t konami_573_network_pcb_unit_pccard_device::read_fifo_bytes(ringbuffer<uint16_t, 0x10000> &buf, uint8_t *output, size_t len)
{
	size_t read_len = 0;

	memset(output, 0, len);

	while (read_len < len)
	{
		auto v = read_fifo_u16(buf);
		output[read_len++] = BIT(v, 0, 8);

		if ((len & 1) == 0)
			output[read_len++] = BIT(v, 8, 8);
	}

	return read_len;
}

void konami_573_network_pcb_unit_pccard_device::process_opcode()
{
	ringbuffer<uint16_t, 0x10000> packet(m_input_buf, (((m_input_buf.at(2) + 1) & 0xfffe) >> 1) + 3);
	const auto expected_head = (m_input_buf.head() + 6 + m_input_buf.at(2)) % m_input_buf.size();

	LOGPACKET("\nCommand: ");
	for (auto i = 0; i < packet.length(); i+=2)
		LOGPACKET("%04x ", packet.at(i >> 1));
	LOGPACKET("\n");

	// Remove copied range
	while (m_input_buf.head() != expected_head)
		m_input_buf.pop();

	asio::dispatch(m_io_service, std::bind(&konami_573_network_pcb_unit_pccard_device::process_opcode_internal, this, packet, std::ref(m_output_buf)));
}

void konami_573_network_pcb_unit_pccard_device::process_opcode_internal(ringbuffer<uint16_t, 0x10000> &input_buf, ringbuffer<uint16_t, 0x10000> &output_buf)
{
	while (output_buf.is_locked());

	output_buf.lock();

	const int arg0 = read_fifo_u16(input_buf);
	const int opcode = arg0 & 0xff;
	const int packet_id = read_fifo_u16(input_buf);
	const int remaining = read_fifo_u16(input_buf);

	LOGPACKET("\nFound command %04x %04x %04x | ", arg0, packet_id, remaining);
	for (auto i = 0; i < remaining; i+=2)
		LOGPACKET("%04x ", input_buf.at(i >> 1));
	LOGPACKET("\n");

	m_packet_id = packet_id;

	if (opcode == OPCODE_WRITE_MEMORY)
	{
		// upload code
		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id); // block addr
		write_fifo_u16(output_buf, 0);
	}
	else if (opcode == OPCODE_READ_MEMORY)
	{
		const uint32_t addr = read_fifo_u32(input_buf);
		const uint16_t reqlen = read_fifo_u16(input_buf);

		// mem copy?
		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, reqlen);

		const auto curlen = output_buf.length();

		if (addr == 0x80e00000 || addr == 0x80800000)
		{
			// Receive NPU ID
			write_fifo_bytes(output_buf, config.npu_id, sizeof(config.npu_id));
		}
		else if (addr == 0x80e00008 || addr == 0x80800008)
		{
			// MAC address
			write_fifo_bytes(output_buf, config.mac_address, sizeof(config.mac_address));
		}
		else if (addr == 0x80e00010 || addr == 0x80800010)
		{
			// ?
			write_fifo_u16(output_buf, 1);
		}

		while (output_buf.length() - curlen < reqlen)
			write_fifo_u16(output_buf, 0);
	}
	else if (opcode == OPCODE_EXECUTE_MEMORY)
	{
		// execute code at offset
		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 0);

		m_state_cur = 0x0f;
	}
	else if (opcode == 0x00)
	{
	}
	else if (opcode == 0x01)
	{
		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 0);
	}
	else if (opcode == 0x37)
	{
		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, 1);
	}
	else if (opcode == 0x38)
	{
		write_fifo_u16(output_buf, 0x39);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, 1);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 0);
	}
	else if (opcode == OPCODE_SOCKET_CLOSE)
	{
		// close socket
		const uint32_t fd = read_fifo_u32(input_buf);

		int ret = 0;

#ifdef _WIN32
		ret = closesocket(fd);
#else
		ret = close(fd);
#endif

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, ret);

		LOGNET("socket close(%d) = %d\n", fd, ret);
	}
	else if (opcode == OPCODE_SOCKET_CONNECT)
	{
		// connect
		const uint32_t fd = read_fifo_u32(input_buf);
		read_fifo_u32(input_buf);
		const uint32_t sin_family = read_fifo_u16(input_buf); // should be sockaddr_in struct
		const uint32_t sin_port = read_fifo_u16(input_buf);
		const uint32_t sin_addr = read_fifo_u32(input_buf);

		struct sockaddr_in dest;
		dest.sin_family = sa_family_t(sin_family),
		dest.sin_port = in_port_t(sin_port),
		dest.sin_addr.s_addr = in_addr_t(sin_addr);

		const int ret = connect(fd, (struct sockaddr*)&dest, sizeof(dest));

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 8);

		write_fifo_u32(output_buf, ret);
		write_fifo_u32(output_buf, 0); // this value actually gets copied into NVRAM and then is read back out for the 0x37 command. What is it?

		LOGNET("socket connect(%d, { family=%d, port=%d, addr=%d }, %d) = %d\n", fd, sin_family, sin_port, sin_addr, sizeof(dest), ret);
	}
	else if (opcode == OPCODE_SOCKET_RECV)
	{
		// recv
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t buffer_len = read_fifo_u32(input_buf);
		[[maybe_unused]] const uint32_t unk = read_fifo_u32(input_buf);

		uint8_t *response = (uint8_t*)calloc(buffer_len, sizeof(uint8_t));
		const ssize_t bytes = recv(fd, (char*)response, buffer_len, 0);
		uint32_t total_message_size = 4;

		if (bytes > 0)
			total_message_size += bytes;

		total_message_size = (total_message_size + 1) & 0xfffe;

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, total_message_size);

		write_fifo_u32(output_buf, bytes);
		write_fifo_bytes(output_buf, response, bytes);

		if (response != nullptr)
			free(response);

		LOGNET("socket recv(%d, ..., %d) = %d\n", fd, buffer_len, bytes);
		for (int i = 0; i < bytes; i++)
			LOGNET("%02x ", response[i]);
		LOGNET("\n");
	}
	else if (opcode == OPCODE_SOCKET_RECVFROM)
	{
		// recvfrom
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t buffer_len = read_fifo_u32(input_buf);
		[[maybe_unused]] const uint32_t unk = read_fifo_u32(input_buf);

		uint8_t *response = (uint8_t*)calloc(buffer_len, sizeof(uint8_t));

		struct sockaddr_in from;
		socklen_t fromlen = sizeof(from);

		ssize_t bytes = recvfrom(fd, (char*)response, buffer_len, 0, (struct sockaddr*)&from, &fromlen);
		ssize_t total_bytes = bytes;
		int total_message_size = 8;

		if (bytes >= 0)
		{
			total_bytes = bytes + 20;
			total_message_size += total_bytes;
		}

		total_message_size = (total_message_size + 1) & 0xfffe;

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, total_message_size);

		write_fifo_u32(output_buf, bytes); // 8
		write_fifo_u32(output_buf, sizeof(from)); // c

		if (bytes >= 0)
		{
			write_fifo_u32(output_buf, sizeof(from)); // 10
			write_fifo_u16(output_buf, from.sin_family); // 14
			write_fifo_u16(output_buf, from.sin_port); // 16
			write_fifo_u32(output_buf, from.sin_addr.s_addr); // 18
			write_fifo_bytes(output_buf, (uint8_t*)from.sin_zero, sizeof(from.sin_zero)); // 1c
			write_fifo_bytes(output_buf, response, bytes); // 24
		}

		if (response != nullptr)
			free(response);

		LOGNET("socket recvfrom(%d, ..., %d, 0, ..., ...) = %d\n", fd, buffer_len, bytes);
		for (int i = 0; i < bytes; i++)
			LOGNET("%02x ", response[i]);
		LOGNET("\n");
	}
	else if (opcode == OPCODE_SOCKET_SEND)
	{
		// send
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t payload_len = read_fifo_u32(input_buf);
		[[maybe_unused]] const uint32_t unk = read_fifo_u32(input_buf);

		uint8_t *payload = (uint8_t*)calloc(((payload_len + 1) & 0xfffe), sizeof(uint8_t));
		read_fifo_bytes(input_buf, payload, (payload_len + 1) & 0xfffe);

		const ssize_t ret = send(fd, (char*)payload, payload_len, 0);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, ret);

		LOGNET("socket send(%d, ..., %d) = %d\n", fd, payload_len, ret);
		for (int i = 0; i < payload_len; i++)
			LOGNET("%02x ", payload[i]);
		LOGNET("\n");

		if (payload != nullptr)
			free(payload);
	}
	else if (opcode == OPCODE_SOCKET_SENDTO)
	{
		// sendto
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t payload_len = read_fifo_u32(input_buf);
		[[maybe_unused]] const uint32_t unk = read_fifo_u32(input_buf);
		[[maybe_unused]] const uint32_t hdr_len = read_fifo_u32(input_buf); // or maybe payload offset?
		const uint32_t sin_family = read_fifo_u16(input_buf); // should be sockaddr_in struct
		const uint32_t sin_port = read_fifo_u16(input_buf);
		const uint32_t sin_addr = read_fifo_u32(input_buf);
		read_fifo_u16(input_buf); // should always be zero
		read_fifo_u16(input_buf);
		read_fifo_u16(input_buf);
		read_fifo_u16(input_buf);

		uint8_t *payload = (uint8_t*)calloc(((payload_len + 1) & 0xfffe), sizeof(uint8_t));
		read_fifo_bytes(input_buf, payload, (payload_len + 1) & 0xfffe);

		struct sockaddr_in dest;
		dest.sin_family = sa_family_t(sin_family),
		dest.sin_port = in_port_t(sin_port),
		dest.sin_addr.s_addr = in_addr_t(sin_addr);

		const ssize_t ret = sendto(fd, (char*)payload, payload_len, 0, (struct sockaddr*)&dest, sizeof(dest));

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 8);

		write_fifo_u32(output_buf, ret);
		write_fifo_u32(output_buf, 0);

		LOGNET("socket sendto(%d, ..., %d, 0, { family=%d, port=%d, addr=%d }, %d) = %d | %d\n", fd, payload_len, sin_family, sin_port, sin_addr, sizeof(dest), ret, hdr_len);
		for (int i = 0; i < payload_len; i++)
			LOGNET("%02x ", payload[i]);
		LOGNET("\n");

		if (payload != nullptr)
			free(payload);
	}
	else if (opcode == OPCODE_SOCKET_SETSOCKOPT)
	{
		// Found command 0a2c 000d 0018 | 0006 0000 ffff 0000 0200 0000 0008 0000 0002 0000 0000 0000
		// int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t level = read_fifo_u32(input_buf);
		const uint32_t optname = read_fifo_u32(input_buf);
		const uint32_t optlen = read_fifo_u32(input_buf);

		uint8_t *optval = (uint8_t*)calloc(optlen, sizeof(uint8_t));
		read_fifo_bytes(input_buf, optval, optlen);

		const uint32_t val = (optval[0] | (optval[1] << 8) | (optval[2] << 16) | (optval[3] << 24)) & 1;
		const int ret = setsockopt(fd, level, optname, (const char*)&val, sizeof(val));

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, ret);

		if (optval != nullptr)
			free(optval);

		LOGNET("socket setsockopt(%d, %d, %d, ...) = %d | %d\n", fd, level, optname, ret, optlen);
	}
	else if (opcode == OPCODE_SOCKET_SOCKET)
	{
		const uint32_t domain = read_fifo_u32(input_buf);
		uint32_t type = read_fifo_u32(input_buf);
		uint32_t protocol = read_fifo_u32(input_buf);

		if (type == 1 && (protocol == 1 || protocol == 0)) // for TCP
		{
			type = SOCK_STREAM;
			protocol = IPPROTO_TCP;
		}
		else if (type == 2 && protocol == 3) // for UDP (used by DNS)
		{
			type = SOCK_DGRAM;
			protocol = IPPROTO_UDP;
		}
		else if (type == 3 && protocol == 2) // for NTP
		{
#ifdef _WIN32
			type = SOCK_RAW;
#else
			type = SOCK_DGRAM;
#endif
			protocol = IPPROTO_ICMP;
		}
		else
		{
			fatalerror("Unknown socket configuration! %d %d %d\n", domain, type, protocol);
		}

		const int socketfd = socket(domain, type, protocol);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, socketfd);

		LOGNET("socket socket(%d, %d, %d) = %d\n", domain, type, protocol, socketfd);
	}
	else if (opcode == OPCODE_NET_GET_SETTINGS)
	{
		const uint32_t addr = read_fifo_u32(input_buf);
		const uint32_t reqlen = read_fifo_u32(input_buf);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, reqlen + 4);
		write_fifo_u32(output_buf, reqlen);

		LOGCMD("net getsettings(0x%x, %d)\n", addr, reqlen);

		if (reqlen > 0)
		{
			write_fifo_u16(output_buf, 0);

			if (addr == REQVAL_ADDR_IP_ADDR)
			{
				write_fifo_bytes(output_buf, config.ip_address, sizeof(config.ip_address));
			}
			else if (addr == REQVAL_ADDR_SUBNET_MASK)
			{
				write_fifo_bytes(output_buf, config.subnet_mask, sizeof(config.subnet_mask));
			}
			else if (addr == REQVAL_ADDR_DEFAULT_GATEWAY)
			{
				write_fifo_bytes(output_buf, config.default_gateway, sizeof(config.default_gateway));
			}
			else if (addr == REQVAL_ADDR_DNS_SERVERS)
			{
				write_fifo_bytes(output_buf, config.dns_server1, sizeof(config.dns_server1));
				write_fifo_bytes(output_buf, config.dns_server2, sizeof(config.dns_server2));
			}
			else if (addr == REQVAL_ADDR_DOMAIN_NAME)
			{
				// Domain name, up to 32 bytes total
				write_fifo_bytes(output_buf, config.domain_name, sizeof(config.domain_name));
			}
			else if (addr == REQVAL_ADDR_DHCP_SERVER)
			{
				write_fifo_bytes(output_buf, config.dhcp_server, sizeof(config.dhcp_server));
			}
			else if (addr == REQVAL_ADDR_NTP_SERVER)
			{
				write_fifo_bytes(output_buf, config.ntp_server, sizeof(config.ntp_server));
			}
			else
			{
				for (int i = 0; i < reqlen - 2; i+=2)
					write_fifo_u16(output_buf, 0);
				fatalerror("Found unhandled value request: %02x\n", addr);
			}
		}
	}
	else if (opcode == OPCODE_FILESYSTEM_INITIALIZE)
	{
		const uint32_t offset = read_fifo_u32(input_buf);
		const uint32_t arg = read_fifo_u32(input_buf);
		auto r = 0;

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_initialize(0x%08x, 0x%x) = %d\n", offset, arg, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_FORMAT)
	{
		auto r = 0;

		char param1[0x40];
		read_fifo_bytes(input_buf, param1, sizeof(param1));

		char param2[0x40];
		read_fifo_bytes(input_buf, param2, sizeof(param2));

		const uint32_t param3 = read_fifo_u32(input_buf);
		const uint32_t param4 = read_fifo_u32(input_buf);
		const uint32_t param5 = read_fifo_u32(input_buf);
		const uint32_t param6 = read_fifo_u32(input_buf);
		const uint32_t param7 = read_fifo_u32(input_buf);
		const uint32_t param8 = read_fifo_u32(input_buf);

		if (strncmp(param1, "atam:", 5) == 0 && m_hdd)
			r = m_hdd->format(param4, param5, param6, param7, param8);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_format(\"%s\", \"%s\", 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x) = %d\n", param1, param2, param3, param4, param5, param6, param7, param8, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_MOUNT)
	{
		auto r = 0;

		char fstype[0x40];
		read_fifo_bytes(input_buf, fstype, sizeof(fstype));

		const uint32_t param2 = read_fifo_u32(input_buf);

		char loadinfo[0x40];
		read_fifo_bytes(input_buf, loadinfo, sizeof(loadinfo));

		const uint32_t param4 = read_fifo_u32(input_buf);

		if (strncmp(loadinfo, "atam:", 5) == 0 && m_hdd)
			r = m_hdd->mount(loadinfo + 5);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_mount(\"%s\", 0x%x, \"%s\", 0x%x) = %d\n", fstype, param2, loadinfo, param4, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_UMOUNT)
	{
		auto r = 0;

		char path[0x40];
		read_fifo_bytes(input_buf, path, sizeof(path));

		if (strncmp(path, "atam:", 5) == 0 && m_hdd)
			r = m_hdd->umount(path + 5);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_umount(\"%s\") = %d\n", path, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_DEVCTL)
	{
		auto r = 0;

		char path[0x40];
		read_fifo_bytes(input_buf, path, sizeof(path));

		const uint32_t reqtype = read_fifo_u32(input_buf);
		const uint32_t param3 = read_fifo_u32(input_buf);
		const uint32_t resplen = read_fifo_u32(input_buf);

		uint8_t *data = (uint8_t*)calloc(resplen, sizeof(uint8_t));
		if (m_hdd && strncmp(path, "atam:", 5) == 0)
			r = m_hdd->devctl(reqtype, param3, resplen, data);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, resplen + 4);
		write_fifo_u32(output_buf, r);
		write_fifo_bytes(output_buf, data, resplen);

		if (data != nullptr)
			free(data);

		LOGFS("pythonfs_devctl(\"%s\", 0x%x, 0x%x, 0x%x) = %d\n", path, reqtype, param3, resplen, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_CHDIR)
	{
		uint32_t r = 0;

		char path[0x100];
		read_fifo_bytes(input_buf, path, sizeof(path));

		if (m_hdd && strncmp(path, "atam:", 5) == 0)
			r = m_hdd->chdir(path + 5);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_chdir(\"%s\") = %d\n", path, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_OPEN)
	{
		uint32_t r = 0;

		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		const uint32_t flags = read_fifo_u32(input_buf);
		const uint32_t mode = read_fifo_u32(input_buf);

		if (m_hdd && strncmp(filename, "atam:", 5) == 0)
			r = m_hdd->open(filename + 5, flags, mode);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_open(\"%s\", 0x%x, 0x%x) = %d\n", filename, flags, mode, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_LSEEK)
	{
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t offset = read_fifo_u32(input_buf);
		const uint32_t whence = read_fifo_u32(input_buf);

		auto r = offset;

		if (m_hdd)
			r = m_hdd->lseek(fd, offset, whence);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_lseek(%d, 0x%x, %d) = %d\n", fd, offset, whence, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_READ)
	{
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t orig_size = read_fifo_u32(input_buf);
		uint32_t r = 0;

		uint32_t size = 0;
		uint8_t *data = (uint8_t*)calloc(orig_size + (2 - (orig_size % 2)), sizeof(uint8_t));

		if (m_hdd && data != nullptr)
		{
			if ((r = m_hdd->read(fd, orig_size, data, size)) < 0)
				size = 0;
		}

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4 + size);

		write_fifo_u32(output_buf, r);

		if (r > 0)
			write_fifo_bytes(output_buf, data, size);

		if (data != nullptr)
			free(data);

		LOGFS("pythonfs_read(%d, 0x%x) = %d | %04x\n", fd, orig_size, r, size);
	}
	else if (opcode == OPCODE_FILESYSTEM_WRITE)
	{
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t len = read_fifo_u32(input_buf);
		uint32_t r = 0;

		uint8_t *data = (uint8_t*)calloc(len, sizeof(uint8_t));

		read_fifo_bytes(input_buf, data, len);

		if (m_hdd)
		{
			if (m_hdd->write(fd, len, data) >= 0)
				r = len;
		}

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		if (data != nullptr)
			free(data);

		LOGFS("pythonfs_write(%d, 0x%x, ...) = %d\n", fd, len, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_CLOSE)
	{
		const uint32_t fd = read_fifo_u32(input_buf);
		auto r = 0;

		if (m_hdd)
			r = m_hdd->close(fd);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_close(%d) = %d\n", fd, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_GETSTAT)
	{
		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		auto r = 0;

		uint8_t data[0x40];
		std::fill(std::begin(data), std::end(data), 0);

		if (m_hdd && strncmp(filename, "atam:", 5) == 0)
			r = m_hdd->getstat(filename + 5, data);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);

		if (r < 0)
		{
			write_fifo_u16(output_buf, 4);
			write_fifo_u32(output_buf, r);
		}
		else
		{
			write_fifo_u16(output_buf, sizeof(data) + 4);
			write_fifo_u32(output_buf, r);
			write_fifo_bytes(output_buf, data, sizeof(data));
		}

		LOGFS("pythonfs_getstat(\"%s\") = %d\n", filename, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_DOPEN)
	{
		uint32_t r = 0;

		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		if (m_hdd && strncmp(filename, "atam:", 5) == 0)
			r = m_hdd->dopen(filename + 5);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_dopen(\"%s\") = %d\n", filename, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_DCLOSE)
	{
		const uint32_t fd = read_fifo_u32(input_buf);
		auto r = 0;

		if (m_hdd)
			r = m_hdd->dclose(fd);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_dclose(%d) = %d\n", fd, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_DREAD)
	{
		const uint32_t fd = read_fifo_u32(input_buf);
		uint32_t r = 0;

		uint8_t data[0x144];
		std::fill(std::begin(data), std::end(data), 0);

		if (m_hdd)
		{
			if (m_hdd->dread(fd, &data[0]) < 0)
				r = 0;
			else
				r = sizeof(data);
		}

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4 + r);

		write_fifo_u32(output_buf, r);
		write_fifo_bytes(output_buf, data, r);

		LOGFS("pythonfs_dread(%d) = %d\n", fd, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_MKDIR)
	{
		uint32_t r = 0;

		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		const uint32_t mode = read_fifo_u32(input_buf);

		if (m_hdd && strncmp(filename, "atam:", 5) == 0)
			r = m_hdd->mkdir(filename + 5, mode);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_mkdir(\"%s\", 0x%x) = %d\n", filename, mode, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_RMDIR)
	{
		uint32_t r = 0;

		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		if (m_hdd && strncmp(filename, "atam:", 5) == 0)
			r = m_hdd->rmdir(filename + 5);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_rmdir(\"%s\") = %d\n", filename, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_REMOVE)
	{
		uint32_t r = 0;

		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		if (m_hdd && strncmp(filename, "atam:", 5) == 0)
			r = m_hdd->remove(filename + 5);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("pythonfs_remove(\"%s\") = %d\n", filename, r);
	}
	else
	{
		if (opcode >= OPCODE_FILESYSTEM_INITIALIZE)
			fatalerror("Implement this opcode: %02x\n", opcode);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 0);
	}

	output_buf.unlock();

	// Add temp buffer data to main output buffer
	LOGPACKET("\nResponse (%04x vs %04x, %04x %04x): ", output_buf.length(), output_buf.at(2) + 6, output_buf.head(), output_buf.tail());
	for (auto i = 0; i < output_buf.at(2) + 6; i+=2)
		LOGPACKET("%04x ", output_buf.at(i >> 1));
	LOGPACKET("\n");
}

void konami_573_network_pcb_unit_pccard_device::parse_ini_file(const char *filename)
{
	util::core_file::ptr inifile = nullptr;;
	auto filerr = util::core_file::open(filename, OPEN_FLAG_READ, inifile);

	if (filerr)
	{
		osd_printf_error("Error opening input file (%s): %s\n", filename, filerr.message());
		return;
	}

	// loop over lines in the file
	char buffer[4096];
	while (inifile->gets(buffer, std::size(buffer)) != nullptr)
	{
		// find the extent of the name
		char *optionname;
		for (optionname = buffer; *optionname != 0; optionname++)
			if (!isspace((uint8_t)*optionname))
				break;

		// skip comments
		if (*optionname == 0 || *optionname == '#')
			continue;

		// scan forward to find the first space
		char *temp;
		for (temp = optionname; *temp != 0; temp++)
			if (isspace((uint8_t)*temp))
				break;

		// if we hit the end early, print a warning and continue
		if (*temp == 0)
		{
			osd_printf_warning("Warning: invalid line in INI: %s", buffer);
			continue;
		}

		// NULL-terminate
		*temp++ = 0;

		// scan forward to the first non-space
		for (; *temp != 0; temp++)
			if (!isspace((uint8_t)*temp))
				break;

		char *optiondata = temp;

		// scan the data, stopping when we hit a comment
		bool inquotes = false;
		for (temp = optiondata; *temp != 0; temp++)
		{
			if (*temp == '"')
				inquotes = !inquotes;
			if (*temp == '#' && !inquotes)
				break;
			if (*temp == '\n' || *temp == '\r')
				break;
		}
		*temp = 0;

		LOG("config: %s %s\n", optionname, optiondata);

		if (core_stricmp(optionname, "npu_id") == 0)
			parse_hexstr(optiondata, config.npu_id, sizeof(config.npu_id));
		else if (core_stricmp(optionname, "mac_address") == 0)
			parse_mac_address(optiondata, config.mac_address, sizeof(config.mac_address));
		else if (core_stricmp(optionname, "ip_address") == 0)
			parse_ip_address(optiondata, config.ip_address, sizeof(config.ip_address));
		else if (core_stricmp(optionname, "subnet_mask") == 0)
			parse_ip_address(optiondata, config.subnet_mask, sizeof(config.subnet_mask));
		else if (core_stricmp(optionname, "default_gateway") == 0)
			parse_ip_address(optiondata, config.default_gateway, sizeof(config.default_gateway));
		else if (core_stricmp(optionname, "dns_server1") == 0)
			parse_ip_address(optiondata, config.dns_server1, sizeof(config.dns_server1));
		else if (core_stricmp(optionname, "dns_server2") == 0)
			parse_ip_address(optiondata, config.dns_server2, sizeof(config.dns_server2));
		else if (core_stricmp(optionname, "dhcp_server") == 0)
			parse_ip_address(optiondata, config.dhcp_server, sizeof(config.dhcp_server));
		else if (core_stricmp(optionname, "ntp_server") == 0)
			parse_ip_address(optiondata, config.ntp_server, sizeof(config.ntp_server));
		else if (core_stricmp(optionname, "domain_name") == 0)
		{
			const int startIdx = optiondata[0] == '"' ? 1 : 0;
			int len = 0;

			for (int i = startIdx; i < strlen(optiondata); i++)
			{
				if (optiondata[i] == '"')
				{
					len = i - startIdx;
					break;
				}
			}

			if (len > 0)
				strncpy(config.domain_name, optiondata + startIdx, len);
		}
	}
}

uint8_t konami_573_network_pcb_unit_pccard_device::str2hex(const char *input, int len)
{
	const char val[3] = {
		len >= 2 ? input[len - 2] : '0',
		len >= 1 ? input[len - 1] : '0',
		0
	};

	return strtoul(val, NULL, 16);
}

void konami_573_network_pcb_unit_pccard_device::parse_hexstr(const char *input, uint8_t* output, size_t outputlen)
{
	// Supports variable size strings
	for (int i = 0; i < strlen(input) && i / 2 < outputlen; i+=2)
	{
		if (i + 1 < strlen(input))
			output[outputlen - 1 - (i / 2)] = str2hex(&input[strlen(input) - i - 1 - 1], 2);
		else
			output[outputlen - 1 - (i / 2)] = str2hex(&input[strlen(input) - i - 1], 1);
	}
}

void konami_573_network_pcb_unit_pccard_device::parse_mac_address(const char *input, uint8_t* output, size_t outputlen)
{
	// 6 groups of 2 hex bytes separated by :
	int curbyte = 0;
	int idx = 0;

	while (idx < strlen(input))
	{
		int idx2 = idx;
		while (idx2 < strlen(input) && input[idx2] != ':')
			idx2++;

		output[curbyte++] = str2hex(input + idx, idx2 - idx);
		idx = idx2 + 1;
	}
}

void konami_573_network_pcb_unit_pccard_device::parse_ip_address(const char *input, uint8_t* output, size_t outputlen)
{
	// 4 digits between 0-255 separated by .
	int curbyte = 0;
	int idx = 0;

	while (idx < strlen(input))
	{
		int idx2 = idx;
		while (idx2 < strlen(input) && input[idx2] != '.')
			idx2++;

		output[curbyte++] = atoi(input + idx);
		idx = idx2 + 1;
	}
}

uint32_t konami_573_network_pcb_unit_pccard_device::parse_ip_address(const char *input)
{
	uint8_t addr[4];
	parse_ip_address(input, addr, sizeof(addr));
	return (addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) | addr[3];
}
