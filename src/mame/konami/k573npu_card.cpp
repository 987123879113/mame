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
# PCBID format: 014001ggffeeddccbbhh
# NPU ID format: 01bbccddeeffgghh
# Fill in xx with your own random hex values
npu_id          01xxxxxxxxxxxxxx

# MAC address of the network PCB unit
# The first 3 bytes must always be 00:06:79 or it will not accept the MAC address
# Fill in the xx with your own random hex values
mac_address     00:06:79:xx:xx:xx

# IP address of the network PCB unit
# This should be the local IP address of your current computer
ip_address      192.168.1.24

# Point to whatever server has the service URL information configured
dns_server1     192.168.1.1
dns_server2     192.168.1.1

# The service URL will become services.<domain_name>
domain_name     ""

# NTP time server, must be able to respond to ICMP pings
# Provided default: pool.ntp.org
ntp_server      162.159.200.123

# If you don't know then just copy what is in your network adapter settings (or look at ipconfig/ifconfig output)
subnet_mask     255.255.255.0
default_gateway 192.168.1.1
dhcp_server     192.168.1.1



DNS SERVER NOTE:
If you do not have an easy way to configure a custom DNS entry through your router or some available server,
you can run your own locally using SimpleDNSServer.py.

https://raw.githubusercontent.com/RockyZ/SimpleDNSServer/master/SimpleDNSServer.py

You can make a text file (named something like "hosts.txt") with the following:
192.168.1.24 services

Replace "192.168.1.24" with the same IP address as k573npu.ini's ip_address.

Then run: "SimpleDNSServer.py hosts.txt" from a command prompt to start the DNS server.
With this, you can set k573npu.ini's dns_server1 and dns_server2 to the same IP address as k573npu.ini's ip_address.


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
	COMMAND_UNSUCCESSFUL = -2,
};

enum
{
	FD_MASK_HDD = 0x00000000,
	FD_MASK_RAM = 0x40000000,
};

const char *get_file_path_from_absolute_path(const char *input)
{
	const char *output = input;

	while (*output != '\0' && *output != ':')
		output++;

	while (*output == ':')
		output++;

	return output;
}

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
	harddisk_image_device *hdd = m_npu_hdd;

	if (!m_npu_hdd->exists())
		hdd = nullptr;

	m_hdd = std::make_unique<konami_573_network_pcb_unit_storage_pythonfs>(
		FD_MASK_HDD,
		new konami_573_network_pcb_unit_storage(hdd)
	);

	m_ramfs = std::make_unique<konami_573_network_pcb_unit_storage_pythonfs>(
		FD_MASK_RAM,
		new konami_573_network_pcb_unit_storage(0x900000)
	);
}

void konami_573_network_pcb_unit_pccard_device::device_reset()
{
	m_hdd->reset();
	m_ramfs->reset();

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

	std::stringstream command_str;
	command_str << "\nCommand: ";
	for (auto i = 0; i < packet.length(); i+=2)
		command_str << std::hex << std::setw(4) << std::setfill('0') << packet.at(i >> 1) << " ";
	command_str << std::endl;
	LOGPACKET(command_str.str());

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

		LOGFS("fs_initialize(0x%08x, 0x%x) = %d\n", offset, arg, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_FORMAT)
	{
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

		auto r = fs_format(param1, param4, param5, param6, param7, param8);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_format(\"%s\", \"%s\", 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x) = %d\n", param1, param2, param3, param4, param5, param6, param7, param8, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_MOUNT)
	{
		char fstype[0x40];
		read_fifo_bytes(input_buf, fstype, sizeof(fstype));

		const uint32_t param2 = read_fifo_u32(input_buf);

		char loadinfo[0x40];
		read_fifo_bytes(input_buf, loadinfo, sizeof(loadinfo));

		const uint32_t param4 = read_fifo_u32(input_buf);

		auto r = fs_mount(loadinfo);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_mount(\"%s\", 0x%x, \"%s\", 0x%x) = %d\n", fstype, param2, loadinfo, param4, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_UMOUNT)
	{
		char path[0x40];
		read_fifo_bytes(input_buf, path, sizeof(path));

		auto r = fs_umount(path);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_umount(\"%s\") = %d\n", path, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_DEVCTL)
	{
		char path[0x40];
		read_fifo_bytes(input_buf, path, sizeof(path));

		const uint32_t reqtype = read_fifo_u32(input_buf);
		const uint32_t param3 = read_fifo_u32(input_buf);
		const uint32_t resplen = read_fifo_u32(input_buf);

		uint8_t *data = (uint8_t*)calloc(resplen, sizeof(uint8_t));

		auto r = fs_devctl(path, reqtype, param3, resplen, data);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, resplen + 4);
		write_fifo_u32(output_buf, r);
		write_fifo_bytes(output_buf, data, resplen);

		if (data != nullptr)
			free(data);

		LOGFS("fs_devctl(\"%s\", 0x%x, 0x%x, 0x%x) = %d\n", path, reqtype, param3, resplen, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_CHDIR)
	{
		char path[0x100];
		read_fifo_bytes(input_buf, path, sizeof(path));

		auto r = fs_chdir(path);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_chdir(\"%s\") = %d\n", path, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_OPEN)
	{
		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		const uint32_t flags = read_fifo_u32(input_buf);
		const uint32_t mode = read_fifo_u32(input_buf);

		auto r = fs_open(filename, flags, mode);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_open(\"%s\", 0x%x, 0x%x) = %d\n", filename, flags, mode, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_LSEEK)
	{
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t offset = read_fifo_u32(input_buf);
		const uint32_t whence = read_fifo_u32(input_buf);

		auto r = fs_lseek(fd, offset, whence);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_lseek(%d, 0x%x, %d) = %d\n", fd, offset, whence, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_READ)
	{
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t orig_size = read_fifo_u32(input_buf);

		uint32_t size = 0;
		uint8_t *data = (uint8_t*)calloc(orig_size + (2 - (orig_size % 2)), sizeof(uint8_t));

		auto r = fs_read(fd, orig_size, data, size);
		if (r < 0)
			size = 0;

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4 + size);

		write_fifo_u32(output_buf, r);

		if (r > 0)
			write_fifo_bytes(output_buf, data, size);

		if (data != nullptr)
			free(data);

		LOGFS("fs_read(%d, 0x%x) = %d | %04x\n", fd, orig_size, r, size);
	}
	else if (opcode == OPCODE_FILESYSTEM_WRITE)
	{
		const uint32_t fd = read_fifo_u32(input_buf);
		const uint32_t len = read_fifo_u32(input_buf);

		uint8_t *data = (uint8_t*)calloc(len, sizeof(uint8_t));

		read_fifo_bytes(input_buf, data, len);

		auto r = fs_write(fd, len, data);
		if (r >= 0)
			r = len;

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		if (data != nullptr)
			free(data);

		LOGFS("fs_write(%d, 0x%x, ...) = %d\n", fd, len, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_CLOSE)
	{
		const uint32_t fd = read_fifo_u32(input_buf);

		auto r = fs_close(fd);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_close(%d) = %d\n", fd, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_GETSTAT)
	{
		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		uint8_t data[0x40];
		std::fill(std::begin(data), std::end(data), 0);

		auto r = fs_getstat(filename, data);

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

		LOGFS("fs_getstat(\"%s\") = %d\n", filename, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_DOPEN)
	{
		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		auto r = fs_dopen(filename);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_dopen(\"%s\") = %d\n", filename, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_DCLOSE)
	{
		const uint32_t fd = read_fifo_u32(input_buf);

		auto r = fs_dclose(fd);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_dclose(%d) = %d\n", fd, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_DREAD)
	{
		const uint32_t fd = read_fifo_u32(input_buf);

		uint8_t data[0x144];
		std::fill(std::begin(data), std::end(data), 0);

		auto r = fs_dread(fd, &data[0]);

		if (r < 0)
			r = 0;
		else
			r = sizeof(data);


		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4 + r);

		write_fifo_u32(output_buf, r);
		write_fifo_bytes(output_buf, data, r);

		LOGFS("fs_dread(%d) = %d\n", fd, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_MKDIR)
	{
		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		const uint32_t mode = read_fifo_u32(input_buf);

		auto r = fs_mkdir(filename, mode);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_mkdir(\"%s\", 0x%x) = %d\n", filename, mode, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_RMDIR)
	{
		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		auto r = fs_rmdir(filename);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_rmdir(\"%s\") = %d\n", filename, r);
	}
	else if (opcode == OPCODE_FILESYSTEM_REMOVE)
	{
		char filename[0x100];
		read_fifo_bytes(input_buf, filename, sizeof(filename));

		auto r = fs_remove(filename);

		write_fifo_u16(output_buf, 0);
		write_fifo_u16(output_buf, packet_id);
		write_fifo_u16(output_buf, 4);

		write_fifo_u32(output_buf, r);

		LOGFS("fs_remove(\"%s\") = %d\n", filename, r);
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
	std::stringstream response_str;
	response_str << "\nResponse ("
		<< std::hex << std::setw(4) << std::setfill('0') << output_buf.length()
		<< " vs "
		<< std::hex << std::setw(4) << std::setfill('0') << output_buf.at(2) + 6
		<< ", "
		<< std::hex << std::setw(4) << std::setfill('0') << output_buf.head()
		<< " "
		<< std::hex << std::setw(4) << std::setfill('0') << output_buf.tail()
		<< "): ";
	for (auto i = 0; i < output_buf.at(2) + 6; i+=2)
		response_str << std::hex << std::setw(4) << std::setfill('0') << output_buf.at(i >> 1) << " ";
	response_str << std::endl;
	LOGPACKET(response_str.str());
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

konami_573_network_pcb_unit_storage_pythonfs *konami_573_network_pcb_unit_pccard_device::get_device_for_fd(int fd)
{
	if ((fd & 0xf0000000) == FD_MASK_HDD)
		return m_hdd.get();
	else if ((fd & 0xf0000000) == FD_MASK_RAM)
		return m_ramfs.get();

	return nullptr;
}

konami_573_network_pcb_unit_storage_pythonfs *konami_573_network_pcb_unit_pccard_device::get_device_for_target(const char *targetdev)
{
	if (strncmp(targetdev, "atam", 4) == 0)
		return m_hdd.get();
	else if (strncmp(targetdev, "ram", 3) == 0)
		return m_ramfs.get();

	return nullptr;
}

int konami_573_network_pcb_unit_pccard_device::fs_open(const char *targetdev, uint32_t flags, uint32_t mode)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->open(get_file_path_from_absolute_path(targetdev), flags, mode);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_close(int fd)
{
	int r = 0;

	auto dev = get_device_for_fd(fd);
	if (dev != nullptr)
		r = dev->close(fd);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_read(int fd, uint32_t len, uint8_t *outbuf, uint32_t &read_count)
{
	int r = 0;

	auto dev = get_device_for_fd(fd);
	if (dev != nullptr)
		r = dev->read(fd, len, outbuf, read_count);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_write(int fd, uint32_t len, uint8_t *data)
{
	int r = 0;

	auto dev = get_device_for_fd(fd);
	if (dev != nullptr)
		r = dev->write(fd, len, data);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_lseek(int fd, uint32_t offset, int whence)
{
	int r = 0;

	auto dev = get_device_for_fd(fd);
	if (dev != nullptr)
		r = dev->lseek(fd, offset, whence);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_dopen(const char *targetdev)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->dopen(get_file_path_from_absolute_path(targetdev));

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_dclose(int fd)
{
	int r = 0;

	auto dev = get_device_for_fd(fd);
	if (dev != nullptr)
		r = dev->dclose(fd);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_dread(int fd, uint8_t *outbuf)
{
	int r = 0;

	auto dev = get_device_for_fd(fd);
	if (dev != nullptr)
		r = dev->dread(fd, outbuf);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_remove(const char *targetdev)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->remove(get_file_path_from_absolute_path(targetdev));

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_mkdir(const char *targetdev, int mode)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->mkdir(get_file_path_from_absolute_path(targetdev), mode);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_rmdir(const char *targetdev)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->rmdir(get_file_path_from_absolute_path(targetdev));

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_getstat(const char *targetdev, uint8_t *outbuf)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->getstat(get_file_path_from_absolute_path(targetdev), outbuf);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_chdir(const char *targetdev)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->chdir(get_file_path_from_absolute_path(targetdev));

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_mount(const char *targetdev)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->mount(get_file_path_from_absolute_path(targetdev));

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_umount(const char *targetdev)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->umount(get_file_path_from_absolute_path(targetdev));

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_devctl(const char *targetdev, uint32_t reqtype, uint32_t param2, uint32_t resplen, uint8_t *data)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->devctl(reqtype, param2, resplen, data);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}

int konami_573_network_pcb_unit_pccard_device::fs_format(const char *targetdev, uint32_t start_lba, uint32_t partition_count1, uint32_t partition_count2, uint32_t param4, uint32_t param5)
{
	int r = 0;

	auto dev = get_device_for_target(targetdev);
	if (dev != nullptr)
		r = dev->format(targetdev, start_lba, partition_count1, partition_count2, param4, param5);

	return r < 0 ? COMMAND_UNSUCCESSFUL : r;
}
