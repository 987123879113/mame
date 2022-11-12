// license:BSD-3-Clause
// copyright-holders:windyfairy

#ifndef MAME_JALECO_JALECO_VJ_UPS_H
#define MAME_JALECO_JALECO_VJ_UPS_H

#pragma once

#include "bus/rs232/rs232.h"

class jaleco_vj_ups_device : public device_t, public device_rs232_port_interface
{
public:
    jaleco_vj_ups_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
    virtual void device_start() override;
    virtual void device_reset() override;
};

DECLARE_DEVICE_TYPE(JALECO_VJ_UPS, jaleco_vj_ups_device)

#endif