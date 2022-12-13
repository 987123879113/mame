// license:BSD-3-Clause
// copyright-holders:windyfairy

#include "emu.h"
#include "rendutil.h"

#include "sdb100.h"

#define LOG_COMMAND    (1 << 1)
// #define VERBOSE      (LOG_COMMAND)
// #define LOG_OUTPUT_STREAM std::cout

#include "logmacro.h"

#define LOGCMD(...)    LOGMASKED(LOG_COMMAND, __VA_ARGS__)


namespace bus::rs232::sdb100
{

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg/pl_mpeg.h"

void app_on_video(plm_t *mpeg, plm_frame_t *frame, void *user)
{
	toshiba_sdb100_device *self = (toshiba_sdb100_device*)user;
	if (self->m_video_bitmap == nullptr)
	{
		// No output video surface
		return;
	}

	plm_frame_to_bgra(frame, self->m_rgb_data, frame->width * 4);

	bitmap_rgb32 video_frame = bitmap_rgb32(
		(uint32_t*)self->m_rgb_data,
		frame->width,
		frame->height,
		frame->width
	);

	copybitmap(
		*self->m_video_bitmap,
		video_frame,
		0, 0, 0, 0,
		rectangle(0, self->m_video_bitmap->width(), 0, self->m_video_bitmap->height())
	);
}

toshiba_sdb100_device::toshiba_sdb100_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, TOSHIBA_SDB100, tag, owner, clock),
	device_serial_interface(mconfig, *this),
	device_rs232_port_interface(mconfig, *this),
	m_data_folder(nullptr),
	m_timer_response(nullptr)
{
}

void toshiba_sdb100_device::device_add_mconfig(machine_config &config)
{
}

static INPUT_PORTS_START(sdb100)
INPUT_PORTS_END

ioport_constructor toshiba_sdb100_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(sdb100);
}

void toshiba_sdb100_device::device_start()
{
	int startbits = 1;
	int databits = 8;
	parity_t parity = PARITY_ODD;
	stop_bits_t stopbits = STOP_BITS_1;

	set_data_frame(startbits, databits, parity, stopbits);

	int txbaud = 9600;
	set_tra_rate(txbaud);

	int rxbaud = 9600;
	set_rcv_rate(rxbaud);

	output_rxd(1);

	// TODO: make this configurable
	output_dcd(0);
	output_dsr(0);
	output_ri(0);
	output_cts(0);

	m_timer_response = timer_alloc(FUNC(toshiba_sdb100_device::send_response), this);

	if (m_data_folder == nullptr)
		m_data_folder = "";
}

void toshiba_sdb100_device::device_reset()
{
	memset(m_command, 0, sizeof(m_command));
	m_command_len = 0;

	m_response_index = sizeof(m_response);

	m_is_powered = false;
	m_title = m_chapter = 0;
	m_playback_status = STATUS_STOP;
	m_plm = nullptr;
	m_rgb_data = nullptr;
	m_wait_timer = 0;
}

void toshiba_sdb100_device::tra_callback()
{
	output_rxd(transmit_register_get_data_bit());
}

void toshiba_sdb100_device::tra_complete()
{
	m_timer_response->adjust(attotime::from_msec(100));
}

TIMER_CALLBACK_MEMBER(toshiba_sdb100_device::send_response)
{
	if (m_response_index < sizeof(m_response) && is_transmit_register_empty())
	{
		// printf("sending %02x\n", m_response[m_response_index]);
		transmit_register_setup(m_response[m_response_index++]);
	}
}

void toshiba_sdb100_device::decode_next_frame(double elapsed_time)
{
	if (m_playback_status == STATUS_PLAYING && m_wait_timer > 0)
	{
		m_wait_timer -= elapsed_time;
	}

	if (m_plm != nullptr && m_playback_status == STATUS_PLAYING && !plm_has_ended(m_plm))
	{
		if (m_wait_timer <= 0)
			plm_decode(m_plm, elapsed_time);
	}
	else
	{
		m_video_bitmap->fill(0xff000000); // Fill with solid black since nothing should be displaying now
	}
}

bool toshiba_sdb100_device::seek_chapter(int title, int chapter)
{
	if (chapter <= 0)
	{
		// Chapters are from 1 and up
		return false;
	}

	if (m_playback_status == STATUS_PLAYING && title == m_title && chapter == m_chapter)
	{
		// Already playing
		return true;
	}

	// printf("Trying to play title %d chapter %d\n", title, chapter);
	m_title = title;
	m_chapter = chapter;

	auto filename_fmt = util::string_format("videos_ppp/%s%strack%d_%d.mpg", m_data_folder ? m_data_folder : "", m_data_folder ? "/" : "", title, chapter);
	auto filename = filename_fmt.c_str();
	m_plm = plm_create_with_filename(filename);
	// printf("Trying to load %s\n", filename);
	if (!m_plm)
	{
		printf("Couldn't open %s\n", filename);
		return false;
	}

	plm_set_audio_enabled(m_plm, false);
	plm_video_set_no_delay(m_plm->video_decoder, true); // The videos are encoded with "-bf 0"

	if (m_rgb_data != nullptr)
		free(m_rgb_data);

	int num_pixels = plm_get_width(m_plm) * plm_get_height(m_plm);
	m_rgb_data = (uint8_t*)malloc(num_pixels * 4);
	plm_set_video_decode_callback(m_plm, app_on_video, this);

	m_wait_timer = 0;//.4;

	if (m_playback_status != STATUS_PAUSE)
		m_playback_status = STATUS_PLAYING;

	return true;
}

void toshiba_sdb100_device::rcv_complete()
{
	receive_register_extract();

	auto c = get_received_char();

	if (c == 0x0d)
	{
		bool status = false;

		// printf("Found command: %s\n", m_command);

		if (m_command[0] == 'R' && m_command[1] == 'E')
		{
			// Reset?
			// Example: RE
		}
		else if (m_command[0] == 'C' && m_command[1] == 'L')
		{
			// Clear?
			// Example: CL
			m_video_bitmap->fill(0xff000000);
		}
		else if (m_command_len == 3 && m_command[0] == 'K' && m_command[1] == 'L')
		{
		}
		else if (m_command[0] == 'P' && m_command[1] == 'L')
		{
			if (m_playback_status == STATUS_STOP)
			{
				// Force video to load again if the video was stopped then started again
				if (!seek_chapter(m_title, m_chapter))
					status = false;
			}

			if (status)
				m_playback_status = STATUS_PLAYING;
		}
		else if (m_command[0] == 'P' && m_command[1] == 'U')
		{
			// Play unpause? Stop?
            if (m_plm != nullptr)
            {
                plm_destroy(m_plm);
                m_plm = nullptr;
            }

			m_playback_status = STATUS_STOP;
		}
		else if (m_command_len == 3 && m_command[0] == 'R' && m_command[1] == 'P')
		{
			// Repeat??
		}
		else if (m_command_len == 8 && m_command[0] == 'C' && m_command[1] == 'S' && m_command[4] == ',')
		{
			// Chapter select
			// Example: CS01,024
			int title, chapter;
			std::sscanf((const char*)m_command, "CS%02d,%03d", &title, &chapter);
			status = seek_chapter(title, chapter);
		}
		else if (m_command_len == 3 && m_command[0] == 'V' && m_command[1] == 'M')
		{
			// Unknown
			// Parameter should only be 0/1
			// Example: VM0, VM1
			if (m_command[2] == '0')
			{
				if (m_plm != nullptr)
				{
					plm_destroy(m_plm);
					m_plm = nullptr;
				}

				m_playback_status = STATUS_STOP;
				m_video_bitmap->fill(0xff000000);
			}
		}

		std::fill(std::begin(m_command), std::end(m_command), 0);
		m_command_len = 0;

		m_response[0] = status ? 'K' : 'E';
		m_response[1] = 0x0d;
		m_response_index = 0;
		m_timer_response->adjust(attotime::from_msec(100));
	}
	else
	{
		m_command[m_command_len] = c;
		m_command_len = (m_command_len + 1) % sizeof(m_command);
	}
}

}

DEFINE_DEVICE_TYPE(TOSHIBA_SDB100, bus::rs232::sdb100::toshiba_sdb100_device, "sdb100", "Toshiba SD-B100")
