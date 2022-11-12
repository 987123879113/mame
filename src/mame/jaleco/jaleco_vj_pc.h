// license:BSD-3-Clause
// copyright-holders:windyfairy

#ifndef MAME_MACHINE_JALECO_VJ_PC_H
#define MAME_MACHINE_JALECO_VJ_PC_H

#include "cpu/i386/i386.h"

DECLARE_DEVICE_TYPE(JALECO_VJ_PC, jaleco_vj_pc_device)

class jaleco_vj_pc_device : public device_t
{
public:
    jaleco_vj_pc_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
    virtual void device_start() override;
    virtual void device_reset() override;
    virtual void device_add_mconfig(machine_config &config) override;

    void superio_config(device_t *device);

    virtual const tiny_rom_entry* device_rom_region() const override;
    virtual ioport_constructor device_input_ports() const override;

private:
    void boot_state_w(uint8_t data);

    required_device<pentium_device> m_maincpu;
};

#endif // MAME_MACHINE_JALECO_VJ_PC_H
