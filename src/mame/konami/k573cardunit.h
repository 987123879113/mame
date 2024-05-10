// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
 * Konami 573 Magnetic Card R/W Unit
 *
 */
#ifndef MAME_MACHINE_K573CARDUNIT_H
#define MAME_MACHINE_K573CARDUNIT_H

#pragma once

#include "k573acio.h"

#include "imagedev/memcard.h"

class k573cardunit_device : public k573acio_node_device, public device_memcard_image_interface
{
public:
	k573cardunit_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	virtual bool is_reset_on_load() const noexcept override { return false; }
	virtual const char *file_extensions() const noexcept override { return "bin"; }
	virtual const char *image_type_name() const noexcept override { return "magcard"; }
	virtual const char *image_brief_type_name() const noexcept override { return "mag"; }

	virtual std::pair<std::error_condition, std::string> call_load() override;
	virtual std::pair<std::error_condition, std::string> call_create(int format_type, util::option_resolution *format_options) override;
	virtual void call_unload() override;

	virtual ioport_constructor device_input_ports() const override;

	DECLARE_INPUT_CHANGED_MEMBER(card_media_update);

protected:
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_config_complete() override;

	virtual void handle_message(std::deque<uint8_t> &message, std::deque<uint8_t> &response, std::deque<uint8_t> &responsepost) override;

private:
	required_ioport m_keypad;

	uint8_t m_card_data[0x80];

	uint32_t m_last_input;
	bool m_card_inserted;
};

DECLARE_DEVICE_TYPE(KONAMI_573_MAGNETIC_CARD_READER, k573cardunit_device)

#endif // MAME_MACHINE_K573CARDUNIT_H
