// license:BSD-3-Clause
// copyright-holders:windyfairy,smf
/*
 * PlayStation Serial Port I/O
 */

#ifndef MAME_CPU_PSX_SIO1_H
#define MAME_CPU_PSX_SIO1_H

#pragma once

#include "diserial.h"


DECLARE_DEVICE_TYPE(PSX_SIO1, psxsio1_device)


class psxsio1_device : public device_t,
    public device_serial_interface
{
    enum {
        SIO1_STATUS_TX_RDY = 1 << 0,
        SIO1_STATUS_RX_RDY = 1 << 1,
        SIO1_STATUS_TX_EMPTY = 1 << 2,
        SIO1_STATUS_RX_PARITY_ERR = 1 << 3,
        SIO1_STATUS_OVERRUN = 1 << 4,
        SIO1_STATUS_RX_FRAMING_ERR = 1 << 5,
        SIO1_STATUS_RX = 1 << 6,
        SIO1_STATUS_DSR = 1 << 7,
        SIO1_STATUS_CTS = 1 << 8,
        SIO1_STATUS_IRQ = 1 << 9
    };

    enum {
        SIO1_CONTROL_TX_ENA = 1 << 0,
        SIO1_CONTROL_DTR = 1 << 1,
        SIO1_CONTROL_RX_ENA = 1 << 2,
        SIO1_CONTROL_TX = 1 << 3,
        SIO1_CONTROL_IACK = 1 << 4,
        SIO1_CONTROL_RTS = 1 << 5,
        SIO1_CONTROL_RESET = 1 << 6,
        SIO1_CONTROL_RX_INT_MODE = 1 << 8,
        SIO1_CONTROL_TX_IENA = 1 << 10,
        SIO1_CONTROL_RX_IENA = 1 << 11,
        SIO1_CONTROL_DSR_IENA = 1 << 12
    };

public:
    psxsio1_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

    // configuration helpers
    auto irq_handler() { return m_irq_handler.bind(); }
    auto txd_handler() { return m_txd_handler.bind(); }
    auto dtr_handler() { return m_dtr_handler.bind(); }
    auto rts_handler() { return m_rts_handler.bind(); }

    void map(address_map &map);

    void data_w(offs_t offset, uint32_t data, uint32_t mem_mask = ~0);
    uint32_t data_r(offs_t offset, uint32_t mem_mask = ~0);

    uint32_t stat_r(offs_t offset, uint32_t mem_mask = ~0);

    void mode_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
    uint16_t mode_r(offs_t offset, uint16_t mem_mask = ~0);

    void ctrl_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
    uint16_t ctrl_r(offs_t offset, uint16_t mem_mask = ~0);

    void baud_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
    uint16_t baud_r(offs_t offset, uint16_t mem_mask = ~0);

    DECLARE_WRITE_LINE_MEMBER(write_rxd);
    DECLARE_WRITE_LINE_MEMBER(write_cts);
    DECLARE_WRITE_LINE_MEMBER(write_dsr);

protected:
    // device-level overrides
    virtual void device_start() override;
    virtual void device_reset() override;

    TIMER_CALLBACK_MEMBER(sio1_transmit_tick);

private:
    void interrupt();
    void sio1_timer_adjust();
    void update_baudrate();

    emu_timer *m_tx_timer;

    util::fifo<uint8_t, 8> m_rx_data;

    uint32_t m_status;
    uint32_t m_mode;
    uint32_t m_control;
    uint32_t m_baud;
    uint8_t m_tx_data;
    uint32_t m_br_factor;
    uint32_t m_rx_int_buf_len;

    devcb_write_line m_irq_handler;
    devcb_write_line m_txd_handler;
    devcb_write_line m_dtr_handler;
    devcb_write_line m_rts_handler;
};

#endif // MAME_CPU_PSX_SIO1_H
