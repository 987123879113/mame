// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 *  Densei UPS for VJ
 */

#include "emu.h"
#include "jaleco_vj_ups.h"


jaleco_vj_ups_device::jaleco_vj_ups_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
    : device_t(mconfig, JALECO_VJ_UPS, tag, owner, clock)
    , device_rs232_port_interface(mconfig, *this)
{
}

void jaleco_vj_ups_device::device_start()
{
}

void jaleco_vj_ups_device::device_reset()
{
    output_cts(1); // line power down
    output_dsr(1); // line shutdown
    output_dcd(0); // line low battery
}

DEFINE_DEVICE_TYPE(JALECO_VJ_UPS, jaleco_vj_ups_device, "rs232_ups", "RS232 Densei UPS")
