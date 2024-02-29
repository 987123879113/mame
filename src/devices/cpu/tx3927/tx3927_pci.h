// license:BSD-3-Clause
// copyright-holders:windyfairy

#ifndef MAME_CPU_MIPS_TX3927_PCI_H
#define MAME_CPU_MIPS_TX3927_PCI_H

#pragma once

#include "machine/pci.h"

class tx3927_pci_device : public pci_bridge_device
{
public:
	tx3927_pci_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	virtual void config_map(address_map &map) override;

protected:
	virtual void device_start() override;
	virtual void device_reset() override;

private:
	uint32_t istat_r();
	void istat_w(uint32_t data);

	uint32_t ipciaddr_r();
	void ipciaddr_w(uint32_t data);

	uint32_t ipcidata_r();
	void ipcidata_w(uint32_t data);

	uint32_t ipcicbe_r();
	void ipcicbe_w(uint32_t data);

	uint32_t m_istat;
	uint32_t m_ipcicbe;
	uint32_t m_ipciaddr;
};

DECLARE_DEVICE_TYPE(TX3927_PCI,      tx3927_pci_device)

#endif // MAME_CPU_MIPS_TX3927_PCI_H
