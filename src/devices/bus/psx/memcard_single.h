// license:BSD-3-Clause
// copyright-holders:Carl,psxAuthor,R. Belmont
#ifndef MAME_BUS_PSX_MEMCARD_SINGLE_H
#define MAME_BUS_PSX_MEMCARD_SINGLE_H

#pragma once


class psxcard_single_device :  public device_t,
						public device_image_interface
{
public:
	psxcard_single_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	virtual iodevice_t image_type() const noexcept override { return IO_MEMCARD; }

	virtual bool is_readable()  const noexcept override { return true; }
	virtual bool is_writeable() const noexcept override { return true; }
	virtual bool is_creatable() const noexcept override { return true; }
	virtual bool must_be_loaded() const noexcept override { return false; }
	virtual bool is_reset_on_load() const noexcept override { return false; }
	virtual const char *file_extensions() const noexcept override { return "mc"; }

	virtual image_init_result call_load() override;
	virtual image_init_result call_create(int format_type, util::option_resolution *format_options) override;

	void disable(bool state) { m_disabled = state; if(state) unload(); }

	bool transfer(uint8_t to, uint8_t *from);

protected:
	virtual void device_start() override;
	virtual void device_reset() override;

private:
	void read_card(const unsigned short addr, unsigned char *buf);
	void write_card(const unsigned short addr, unsigned char *buf);
	unsigned char checksum_data(const unsigned char *buf, const unsigned int sz);

	unsigned char pkt[0x8b], pkt_ptr, pkt_sz, cmd;
	unsigned short addr;
	int state;
	bool m_disabled;
};

// device type definition
DECLARE_DEVICE_TYPE(PSXCARD_SINGLE, psxcard_single_device)

#endif // MAME_BUS_PSX_MEMCARD_SINGLE_H
