// license:BSD-3-Clause
// copyright-holders:Carl,psxAuthor,R. Belmont
/*
    psxcard.c - Sony PlayStation memory card device

    by pSXAuthor
    MESS conversion by R. Belmont
*/

#include "emu.h"
#include "memcard_single.h"

//
//
//

//#define debug_card

//
//
//

static constexpr int block_size = 128;
static constexpr int card_size = block_size * 1024;

DEFINE_DEVICE_TYPE(PSXCARD_SINGLE, psxcard_single_device, "psxcard", "Sony PSX Memory Card")

enum transfer_states
{
	state_illegal=0,
	state_command,
	state_cmdack,
	state_wait,
	state_addr_hi,
	state_addr_lo,
	state_read,
	state_write,
	state_writeack_2,
	state_writechk,
	state_end
};

psxcard_single_device::psxcard_single_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, PSXCARD_SINGLE, tag, owner, clock),
	device_image_interface(mconfig, *this),
	pkt_ptr(0),
	pkt_sz(0),
	cmd(0),
	addr(0),
	state(0),
	m_disabled(false)
{
}

void psxcard_single_device::device_start()
{
	m_disabled = false;

	// save state registrations
	save_item(NAME(pkt));
	save_item(NAME(pkt_ptr));
	save_item(NAME(pkt_sz));
	save_item(NAME(cmd));
	save_item(NAME(addr));
	save_item(NAME(state));
	save_item(NAME(m_disabled));
}

void psxcard_single_device::device_reset()
{
	state = state_illegal;
	memset(pkt, 0, 0x8b);
	pkt_ptr = pkt_sz = cmd = 0;
	addr = 0;
	m_disabled = false;
}

bool psxcard_single_device::transfer(uint8_t to, uint8_t *from)
{
	bool ret=true;

	#ifdef debug_card
        //printf("card: transfer to=%02x from=%02x ret=%c\n",to,*from,ret ? 'T' : 'F');
	#endif

	switch (state)
	{
		case state_illegal:
			if (is_loaded())
			{
				#ifdef debug_card
					printf("CARD: begin\n");
				#endif

				state = state_command;
				*from = 0x00;
			}
			else
			{
				#ifdef debug_card
					printf("CARD: not loaded\n");
				#endif

				ret = false;
			}
			break;

		case state_command:
			cmd=to;
			*from=0x5a;
			state=state_cmdack;
			#ifdef debug_card
            	printf("state_command: %02x, addr = %x\n", to, addr);
			#endif
			break;

		case state_cmdack:
			*from=0x5d;
			state=state_wait;
			#ifdef debug_card
            	printf("state_cmdack: %02x, addr = %x\n", to, addr);
			#endif
			break;

		case state_wait:
			*from=0x00;
			state=state_addr_hi;
			#ifdef debug_card
            	printf("state_wait: %02x, addr = %x\n", to, addr);
			#endif
			break;

		case state_addr_hi:
			addr=(to<<8);
			#ifdef debug_card
            	printf("addr_hi: %02x, addr = %x\n", to, addr);
			#endif
			*from=to;
			state=state_addr_lo;
			break;

		case state_addr_lo:
			addr|=(to&0xff);

			#ifdef debug_card
            	printf("addr_lo: %02x, addr = %x, cmd = %x\n", to, addr, cmd);
			#endif

			switch (cmd)
			{
				case 'R':   // 0x52
				{
					printf("memcard: reading addr = %x\n", addr);
					pkt[0]=*from=0x5c;
					pkt[1]=0x5d;
					pkt[2]=(addr>>8);
					pkt[3]=(addr&0xff);
					read_card(addr,&pkt[4]);
					pkt[4+128]=checksum_data(&pkt[2],128+2);
					pkt[5+128]=0x47;
					pkt_sz=6+128;
					pkt_ptr=1;
					state=state_read;
					break;
				}
				case 'W':   // 0x57
				{
					printf("memcard: writing addr = %x\n", addr);
					pkt[0]=addr>>8;
					pkt[1]=addr&0xff;
					pkt_sz=129+2;
					pkt_ptr=2;
					state=state_write;
					*from=to;
					break;
				}
				default:
					state=state_illegal;
					break;
			}
			break;

		case state_read:
			//assert(to==0);
			*from=pkt[pkt_ptr++];

			#ifdef debug_card
            	//printf("state_read: pkt_ptr = %d, pkt_sz = %d, out = %02x\n", pkt_ptr, pkt_sz, *from);
			#endif

			if (pkt_ptr==pkt_sz)
			{
				#ifdef debug_card
					printf("card: read finished\n");
				#endif

				state=state_end;
			}
			break;

		case state_write:
			*from=to;
			pkt[pkt_ptr++]=to;
			if (pkt_ptr==pkt_sz)
			{
				*from=0x5c;
				state=state_writeack_2;
			}
			break;

		case state_writeack_2:
			*from=0x5d;
			state=state_writechk;
			break;

		case state_writechk:
		{
			unsigned char chk=checksum_data(pkt,128+2);
			if (chk==pkt[128+2])
			{
				#ifdef debug_card
					printf("card: write ok\n");
				#endif

				write_card(addr,pkt+2);

				*from='G';
			} else
			{
				#ifdef debug_card
					printf("card: write fail\n");
				#endif

				*from='N';
			}
			state=state_end;
			break;
		}

		case state_end:
			ret = false;
			state = state_illegal;
			#ifdef debug_card
            	printf("state_end: %02x, addr = %x\n", to, addr);
			#endif
			break;

		default: /*assert(0);*/ ret=false; break;
	}

	return ret;
}

void psxcard_single_device::read_card(const unsigned short addr, unsigned char *buf)
{
	#ifdef debug_card
		printf("card: read block %d\n",addr);
	#endif

	if (addr<(card_size/block_size))
	{
		fseek(addr*block_size, SEEK_SET);
		fread(buf, block_size);
	} else
	{
		memset(buf,0,block_size);
	}
}

void psxcard_single_device::write_card(const unsigned short addr, unsigned char *buf)
{
	#ifdef debug_card
		printf("card: write block %d\n",addr);
	#endif

	if (addr<(card_size/block_size))
	{
		fseek(addr*block_size, SEEK_SET);
		fwrite(buf, block_size);
	}
}

unsigned char psxcard_single_device::checksum_data(const unsigned char *buf, const unsigned int sz)
{
	unsigned char chk=*buf++;
	int left=sz;
	while (--left) chk^=*buf++;
	return chk;
}

image_init_result psxcard_single_device::call_load()
{
	if(m_disabled)
	{
		logerror("psxcard: port disabled\n");
		return image_init_result::FAIL;
	}

	if(length() != card_size)
		return image_init_result::FAIL;
	return image_init_result::PASS;
}

image_init_result psxcard_single_device::call_create(int format_type, util::option_resolution *format_options)
{
	uint8_t block[block_size];
	int i, ret;

	if(m_disabled)
	{
		logerror("psxcard: port disabled\n");
		return image_init_result::FAIL;
	}

	memset(block, '\0', block_size);
	for(i = 0; i < (card_size/block_size); i++)
	{
		ret = fwrite(block, block_size);
		if(ret != block_size)
			return image_init_result::FAIL;
	}
	return image_init_result::PASS;
}
