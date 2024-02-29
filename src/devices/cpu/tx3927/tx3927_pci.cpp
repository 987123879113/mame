// license:BSD-3-Clause
// copyright-holders:windyfairy

#include "emu.h"
#include "cpu/tx3927/tx3927_pci.h"

#include <iostream>

DEFINE_DEVICE_TYPE(TX3927_PCI,      tx3927_pci_device,    "tx3927",  "Toshiba TX3927 (PCI Controller)")

tx3927_pci_device::tx3927_pci_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	pci_bridge_device(mconfig, TX3927_PCI, tag, owner, clock)
{
}

void tx3927_pci_device::device_start()
{
	pci_bridge_device::device_start();
	set_ids(0x102f000a, 0x00, 0x060000, 0x00000000);
}

void tx3927_pci_device::device_reset()
{
	m_istat = 0;
	m_ipciaddr = 0;
}

void tx3927_pci_device::config_map(address_map &map)
{
	pci_bridge_device::config_map(map);

	map(0x044, 0x047).rw(FUNC(tx3927_pci_device::istat_r), FUNC(tx3927_pci_device::istat_w));
	map(0x150, 0x153).rw(FUNC(tx3927_pci_device::ipciaddr_r), FUNC(tx3927_pci_device::ipciaddr_w));
	map(0x154, 0x157).rw(FUNC(tx3927_pci_device::ipcidata_r), FUNC(tx3927_pci_device::ipcidata_w));
	map(0x158, 0x15b).rw(FUNC(tx3927_pci_device::ipcicbe_r), FUNC(tx3927_pci_device::ipcicbe_w));
}

uint32_t tx3927_pci_device::istat_r()
{
	// 0xFFFE_D044 ISTAT Initiator Status Register
	printf("istat_r called %08x\n", m_istat);
	return m_istat;
}

void tx3927_pci_device::istat_w(uint32_t data)
{
	// 0xFFFE_D044 ISTAT Initiator Status Register
	printf("istat_w called %08x\n", data);
	m_istat = data;
}

uint32_t tx3927_pci_device::ipciaddr_r()
{
	// 0xFFFE_D150 IPCIADDR Initiator Indirect Address Register
	printf("ipciaddr_r called\n");
	return 0;
}

void tx3927_pci_device::ipciaddr_w(uint32_t data)
{
	// 0xFFFE_D150 IPCIADDR Initiator Indirect Address Register
	printf("ipciaddr_w called %08x\n", data);
	m_ipciaddr = data;
}

uint32_t tx3927_pci_device::ipcidata_r()
{
	uint32_t r = 0xffffffff;
	const int cmd = BIT(m_ipcicbe, 4, 4);

	// 0xFFFE_D154 IPCIDATA Initiator Indirect Data Register
	if (cmd == 0xa) {
		// configuration space
		const int bus_val = m_ipciaddr >> 11;
		int bus = 0;
		const int device = BIT(m_ipciaddr, 8, 3);
		const int reg = m_ipciaddr & 0xfc;

		for (int i = 0; i < 32; i++)
		{
			if (BIT(bus_val, i)) {
				bus = i;
				break;
			}
		}

		printf("Reading config for %02x %d %d\n", bus, device, reg);

		if(bus == 0x00)
			r = do_config_read(bus, device, reg, 0xffffffff);
		else
			r = propagate_config_read(bus, device, reg, 0xffffffff);
	}

	printf("ipcidata_r called %08x (cmd: %x)\n", r, cmd);

	return r;
}

void tx3927_pci_device::ipcidata_w(uint32_t data)
{
	// 0xFFFE_D154 IPCIDATA Initiator Indirect Data Register
	printf("ipcidata_w called %08x\n", data);
}

uint32_t tx3927_pci_device::ipcicbe_r()
{
	// 0xFFFE_D158 IPCICBE Initiator Indirect Command/Byte Enable Register
	printf("ipcicbe_r called\n");
	return m_ipcicbe;
}

void tx3927_pci_device::ipcicbe_w(uint32_t data)
{
	// 0xFFFE_D158 IPCICBE Initiator Indirect Command/Byte Enable Register
	/*
	0000 Interrupt-acknowledge
	0001 Special-cycle
	0010 I/O read
	0011 I/O write
	0100 (Reserved)
	0101 (Reserved)
	0110 Memory read
	0111 Memory write
	1000 (Reserved)
	1001 (Reserved)
	1010 Configuration read
	1011 Configuration write
	1100 Memory read multiple
	1101 Dual address cycle
	1110 Memory read line
	1111 Memory write and invalidate
	*/
	printf("ipcicbe_w called %08x\n", data);
	m_istat |= 0x1000;
	m_ipcicbe = data;
}
