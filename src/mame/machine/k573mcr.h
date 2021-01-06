// license:BSD-3-Clause
// copyright-holders:smf
/*
 * Konami 573 Memory Card Reader
 *
 */
#ifndef MAME_MACHINE_K573_MCR_H
#define MAME_MACHINE_K573_MCR_H

#pragma once



DECLARE_DEVICE_TYPE(KONAMI_573_MEMORY_CARD_READER, k573mcr_device)

class k573mcr_device : public device_t
{
public:
	k573mcr_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	void jvs_input_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	uint16_t jvs_input_r(offs_t offset, uint16_t mem_mask = ~0);

	void jvs_output_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	uint16_t jvs_output_r(offs_t offset, uint16_t mem_mask = ~0);

	DECLARE_READ_LINE_MEMBER( sense_r );
	DECLARE_READ_LINE_MEMBER( rx_r );
	DECLARE_READ_LINE_MEMBER( tx_r );

protected:
	virtual void device_start() override;

	virtual const tiny_rom_entry *device_rom_region() const override;

private:
	bool is_valid_command();
	void process_command();

	bool rx, tx, sense;

	int input_idx_r, input_idx_w;
	int output_idx_r, output_idx_w;
	uint8_t input_buffer[0x1000];
	uint8_t output_buffer[0x1000];
};

#endif // MAME_MACHINE_K573_MCR_H
