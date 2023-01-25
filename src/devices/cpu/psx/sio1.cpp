// license:BSD-3-Clause
// copyright-holders:windyfairy,smf
/*
 * PlayStation Serial Port I/O
 */

#include "emu.h"
#include "sio1.h"


// #include <iostream>
#define LOG_GENERAL    (1 << 0)
#define LOG_TIMER      (1 << 1)
#define LOG_INTERRUPT  (1 << 2)
#define LOG_TXRX       (1 << 3)
// #define VERBOSE        (LOG_GENERAL | LOG_TIMER | LOG_INTERRUPT | LOG_TXRX)
// #define LOG_OUTPUT_STREAM std::cout

#include "logmacro.h"


DEFINE_DEVICE_TYPE(PSX_SIO1, psxsio1_device, "psxsio1", "Sony PSX SIO-1")

psxsio1_device::psxsio1_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
    device_t(mconfig, PSX_SIO1, tag, owner, clock),
    device_serial_interface(mconfig, *this),
    m_irq_handler(*this),
    m_txd_handler(*this),
    m_dtr_handler(*this),
    m_rts_handler(*this)
{
}

void psxsio1_device::device_start()
{
    m_irq_handler.resolve_safe();
    m_txd_handler.resolve_safe();
    m_dtr_handler.resolve_safe();
    m_rts_handler.resolve_safe();

    m_tx_timer = timer_alloc(FUNC(psxsio1_device::sio1_transmit_tick), this);

    save_item(NAME(m_status));
    save_item(NAME(m_mode));
    save_item(NAME(m_control));
    save_item(NAME(m_baud));
    save_item(NAME(m_tx_data));
    save_item(NAME(m_br_factor));
    save_item(NAME(m_rx_int_buf_len));
    // save_item(NAME(m_rx_data));
}

void psxsio1_device::device_reset()
{
    m_txd_handler(1);
    m_rts_handler(1);
    m_dtr_handler(1);

    transmit_register_reset();
    receive_register_reset();

    m_status = SIO1_STATUS_TX_EMPTY | SIO1_STATUS_TX_RDY | SIO1_STATUS_CTS | SIO1_STATUS_DSR;
    m_mode = 0;
    m_control = 0;
    m_baud = 0;
    m_br_factor = 0;
    m_rx_int_buf_len = 1;
    m_tx_data = 0;
    m_rx_data.clear();
}

void psxsio1_device::map(address_map &map)
{
    map.global_mask(0xf);
    map(0x0, 0x3).rw(FUNC(psxsio1_device::data_r), FUNC(psxsio1_device::data_w));
    map(0x4, 0x7).r(FUNC(psxsio1_device::stat_r));
    map(0x8, 0x9).rw(FUNC(psxsio1_device::mode_r), FUNC(psxsio1_device::mode_w));
    map(0xa, 0xb).rw(FUNC(psxsio1_device::ctrl_r), FUNC(psxsio1_device::ctrl_w));
    // 0xc-0xd is a weird internal register that shouldn't be used as per psx-spx docs
    map(0xe, 0xf).rw(FUNC(psxsio1_device::baud_r), FUNC(psxsio1_device::baud_w));
}

void psxsio1_device::interrupt()
{
    LOGMASKED(LOG_INTERRUPT, "interrupt(%s)\n", tag());
    m_status |= SIO1_STATUS_IRQ;
    m_irq_handler(1);
}

void psxsio1_device::sio1_timer_adjust()
{
    // Baudrate reload factor will be 0 when timer is meant to be stopped
    attotime n_time = attotime::from_hz(33868800) * m_br_factor;

    if (m_baud != 0)
        n_time *= m_baud;

    if (!n_time.is_zero())
        LOGMASKED(LOG_TIMER, "sio1_timer_adjust(%s) = %s (%d x %d)\n", tag(), n_time.as_string(), m_br_factor, m_baud);

    m_tx_timer->adjust(n_time);
}

void psxsio1_device::update_baudrate()
{
    parity_t parity = PARITY_NONE;
    switch (m_mode & 0x30)
    {
        case 0x10:
            LOG("parity: Odd\n");
            parity = PARITY_ODD;
            break;

        case 0x30:
            LOG("parity: Even\n");
            parity = PARITY_EVEN;
            break;

        default:
            LOG("parity: None\n");
            break;
    }

    stop_bits_t stop_bits = STOP_BITS_0;
    switch (m_mode & 0xc0)
    {
        case 0x40:
            LOG("stop bit: 1 bit\n");
            stop_bits = STOP_BITS_1;
            break;

        case 0x80:
            LOG("stop bit: 1.5 bits\n");
            stop_bits = STOP_BITS_1_5;
            break;

        case 0xc0:
            LOG("stop bit: 2 bits\n");
            stop_bits = STOP_BITS_2;
            break;

        default:
            LOG("stop bit: 0 bits\n");
            break;
    }

    int data_bits_count = BIT(m_mode, 2, 2) + 5;
    set_data_frame(1, data_bits_count, parity, stop_bits);
    receive_register_reset();

    switch (m_mode & 0x03)
    {
        case 1:
            LOG("baudrate reload factor: 1\n");
            m_br_factor = 1;
            break;

        case 2:
            LOG("baudrate reload factor: 16\n");
            m_br_factor = 16;
            break;

        case 3:
            LOG("baudrate reload factor: 64\n");
            m_br_factor = 64;
            break;

        default:
            LOG("baudrate reload factor: 0\n");
            m_br_factor = 0;
            break;
    }

    set_rate(33868800, std::max(m_br_factor * m_baud, m_br_factor));
}

TIMER_CALLBACK_MEMBER(psxsio1_device::sio1_transmit_tick)
{
    if ((m_status & SIO1_STATUS_CTS) && (m_control & SIO1_CONTROL_TX_ENA))
    {
        if (!is_transmit_register_empty())
            m_txd_handler(transmit_register_get_data_bit());

        if (is_transmit_register_empty())
        {
            if (!(m_status & SIO1_STATUS_TX_RDY))
            {
                transmit_register_setup(m_tx_data);
                m_status &= ~SIO1_STATUS_TX_EMPTY;
                m_status |= SIO1_STATUS_TX_RDY;
            }
            else
            {
                m_status |= SIO1_STATUS_TX_EMPTY;
            }
        }

        if ((m_control & SIO1_CONTROL_TX_IENA) && (m_status & SIO1_STATUS_TX_RDY) && (m_status & SIO1_STATUS_TX_EMPTY))
            interrupt();

        sio1_timer_adjust();
    }
}

void psxsio1_device::data_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
    LOGMASKED(LOG_TXRX, "%s data_w %02x\n", tag(), data);

    if (!(m_status & SIO1_STATUS_CTS) || !(m_control & SIO1_CONTROL_TX_ENA))
        return;

    m_tx_data = data & 0xff;
    m_status &= ~(SIO1_STATUS_TX_RDY | SIO1_STATUS_TX_EMPTY);

    sio1_timer_adjust();
}

uint32_t psxsio1_device::data_r(offs_t offset, uint32_t mem_mask)
{
    // TODO (psx-spx):
    // Data should be read only via 8bit memory access (the 16bit/32bit "preview" feature is rather unusable).
    // According to the docs, 16-bit reads don't remove from the FIFO but 32-bit reads do
    uint32_t r = 0;

    if (m_status & SIO1_STATUS_RX_RDY)
    {
        if (!m_rx_data.empty())
            r = m_rx_data.dequeue();

        if (m_rx_data.empty())
            m_status &= ~SIO1_STATUS_RX_RDY;
    }

    LOGMASKED(LOG_TXRX, "%s data_r %02x\n", tag(), r);

    return r;
}

uint32_t psxsio1_device::stat_r(offs_t offset, uint32_t mem_mask)
{
    // TODO (psx-spx):
    //   11-25 Baudrate Timer    (15bit timer, decrementing at 33MHz)
    LOG("%s stat_r %08x\n", tag(), m_status);
    return m_status;
}

void psxsio1_device::mode_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
    LOG("%s mode_w %04x\n", tag(), data);

    m_mode = data;

    update_baudrate();
    sio1_timer_adjust();
}

uint16_t psxsio1_device::mode_r(offs_t offset, uint16_t mem_mask)
{
    LOG("%s mode_r %04x\n", tag(), m_mode);
    return m_mode;
}

void psxsio1_device::ctrl_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
    LOG("%s ctrl_w %04x\n", tag(), data);

    m_control = data;
    m_rx_int_buf_len = 1 << BIT(m_control, SIO1_CONTROL_RX_INT_MODE, 2);

    if (m_control & SIO1_CONTROL_RESET)
    {
        LOG("ctrl_w reset\n");

        m_control = 0;
        m_status &= SIO1_STATUS_CTS | SIO1_STATUS_DSR;
        m_mode = 0;
        m_baud = 0;
        m_br_factor = 0;
        m_rx_int_buf_len = 1;
        m_tx_data = 0;
        m_rx_data.clear();
    }

    if (m_control & SIO1_CONTROL_IACK)
    {
        LOG("ctrl_w iack\n");

        m_status &= ~(SIO1_STATUS_OVERRUN | SIO1_STATUS_RX_PARITY_ERR | SIO1_STATUS_RX_FRAMING_ERR | SIO1_STATUS_IRQ);
        m_control &= ~SIO1_CONTROL_IACK;

        m_irq_handler(0);
    }

    if (!(m_control & SIO1_CONTROL_RX_ENA))
    {
        m_status &= ~SIO1_STATUS_RX_RDY;
        m_rx_data.clear();
    }

    m_rts_handler((m_control & SIO1_CONTROL_RTS) == 0);
    m_dtr_handler((m_control & SIO1_CONTROL_DTR) == 0);
}

uint16_t psxsio1_device::ctrl_r(offs_t offset, uint16_t mem_mask)
{
    LOG("%s ctrl_r %04x\n", tag(), m_control);
    return m_control;
}

void psxsio1_device::baud_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
    LOG("%s baud_w %04x\n", tag(), data);
    m_baud = data;
    update_baudrate();
    sio1_timer_adjust();
}

uint16_t psxsio1_device::baud_r(offs_t offset, uint16_t mem_mask)
{
    LOG("%s baud_r %04x\n", tag(), m_baud);
    return m_baud;
}

WRITE_LINE_MEMBER(psxsio1_device::write_rxd)
{
    if (!(m_control & SIO1_CONTROL_RX_ENA))
        return;

    receive_register_update_bit(state);

    if (is_receive_register_full())
    {
        receive_register_extract();

        if (is_receive_parity_error())
            m_status |= SIO1_STATUS_RX_PARITY_ERR;
        if (is_receive_framing_error())
            m_status |= SIO1_STATUS_RX_FRAMING_ERR;

        if (m_rx_data.full())
        {
            m_status |= SIO1_STATUS_OVERRUN;
            m_rx_data.poke(get_received_char());
        }
        else
        {
            m_rx_data.enqueue(get_received_char());
            m_status |= SIO1_STATUS_RX_RDY;
        }

        if (m_control & SIO1_CONTROL_RX_IENA)
        {
            // IRQ when RX FIFO contains 1, 2, 4, 8 bytes
            if (m_rx_data.queue_length() >= m_rx_int_buf_len)
                interrupt();
        }
    }
}

WRITE_LINE_MEMBER(psxsio1_device::write_cts)
{
    if (state)
        m_status |= SIO1_STATUS_CTS;
    else
        m_status &= ~SIO1_STATUS_CTS;
}

WRITE_LINE_MEMBER(psxsio1_device::write_dsr)
{
    if (state)
        m_status |= SIO1_STATUS_DSR;
    else
        m_status &= ~SIO1_STATUS_DSR;

    if ((m_control & SIO1_CONTROL_DSR_IENA) && (m_status & SIO1_STATUS_DSR))
        interrupt();
}
