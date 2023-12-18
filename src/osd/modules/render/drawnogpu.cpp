// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Antonio Giner, Sergi Clara
//============================================================
//
//  drawgnopu.cpp - nogpu drawing
//
//============================================================

#include "render_module.h"

#include "modules/osdmodule.h"

#include "window.h"

// emu
#include "emu.h"
#include "rendersw.hxx"


#include "render_module.h"

// standard windows headers
#include <windows.h>
#include <winsock2.h>

// MAMEOS headers
#if defined(OSD_WINDOWS)
#include "winmain.h"
#elif defined(OSD_SDL)
#include "osdsdl.h"
#endif

#include <lz4/lz4.h>
#include <lz4/lz4hc.h>

#include <switchres/switchres.h>

#define MAX_BUFFER_WIDTH 768
#define MAX_BUFFER_HEIGHT 576
#define VRAM_BUFFER_SIZE 65536

// nogpu UDP server
#define UDP_PORT 32100

// Server commands
#define CMD_CLOSE 1
#define CMD_INIT 2
#define CMD_SWITCHRES 3
#define CMD_BLIT 4
#define CMD_GET_STATUS 5

#pragma pack(1)

typedef struct nogpu_modeline
{
	double    pclock;
	uint16_t  hactive;
	uint16_t  hbegin;
	uint16_t  hend;
	uint16_t  htotal;
	uint16_t  vactive;
	uint16_t  vbegin;
	uint16_t  vend;
	uint16_t  vtotal;
	uint8_t  interlace;
} nogpu_modeline;

typedef struct nogpu_status
{
	uint16_t vcount;
	uint32_t frame_num;
} nogpu_status;

typedef struct cmd_init
{
	const uint8_t cmd = CMD_INIT;
	uint8_t	compression;
	uint16_t block_size;
} cmd_init;

typedef struct cmd_close
{
	const uint8_t cmd = CMD_CLOSE;
} cmd_close;

typedef struct cmd_switchres
{
	const uint8_t cmd = CMD_SWITCHRES;
	nogpu_modeline mode;
} cmd_switchres;

typedef struct cmd_blit
{
	const uint8_t cmd = CMD_BLIT;
	uint32_t frame;
	uint16_t vsync;
	uint16_t block_size;
} cmd_blit;


typedef struct cmd_get_status
{
	const uint8_t cmd = CMD_GET_STATUS;
} cmd_get_status;

#pragma pack(0)


namespace osd {

namespace {

// renderer_nogpu is the information for the current screen
class renderer_nogpu : public osd_renderer
{
public:
	renderer_nogpu(osd_window &window)
		: osd_renderer(window)
		, m_bmdata(nullptr)
		, m_bmsize(0)
	{
	}

	virtual ~renderer_nogpu();
	virtual int create() override;
	virtual render_primitive_list *get_primitives() override;
	virtual int draw(const int update) override;
	virtual void save() override {}
	virtual void record() override {}
	virtual void toggle_fsfx() override {}

private:
	BITMAPINFO                  m_bminfo;
	std::unique_ptr<uint8_t []> m_bmdata;
	size_t                      m_bmsize;

	// npgpu private members
	bool m_initialized = false;
	bool m_first_blit = true;
	int m_compression = 0;
	int m_frame = 0;
	int m_width = 0;
	int m_height = 0;
	int m_vtotal = 0;
	int m_vsync_scanline = 0;
	double m_period = 16.666667;
	nogpu_status m_status;
	modeline m_current_mode;

	osd_ticks_t time_start = 0;
	osd_ticks_t time_entry = 0;
	osd_ticks_t time_blit = 0;
	osd_ticks_t time_exit = 0;

	SOCKET m_sockfd = INVALID_SOCKET;
	char m_fb[MAX_BUFFER_HEIGHT * MAX_BUFFER_WIDTH * 3];
	char m_fb_compressed[MAX_BUFFER_HEIGHT * MAX_BUFFER_WIDTH * 3];
	char inp_buf[2][MAX_BUFFER_WIDTH * 16 * 3 + 1];

	SOCKADDR_IN m_server_addr;
	WSABUF m_databuf;
	WSABUF m_databuf_recv;
	WSAOVERLAPPED m_overlapped;
	WSAOVERLAPPED m_overlapped_recv;

	bool nogpu_init();
	bool nogpu_send_command(void *command, int command_size);
	bool nogpu_switch_video_mode();
	void nogpu_blit(uint32_t frame, uint16_t vsync, uint16_t line_width);
	void nogpu_send_mtu(char *buffer, int bytes_to_send, int chunk_max_size);
	void nogpu_send_lz4(char *buffer, int bytes_to_send, int block_size);
	int nogpu_compress(int id_compress, char *buffer_comp, const char *buffer_rgb, uint32_t buffer_size);
	bool nogpu_get_status(nogpu_status *status, double timeout);
	bool nogpu_wait_status(nogpu_status *status, double timeout);
};

inline double get_ms(osd_ticks_t ticks) { return (double) ticks / osd_ticks_per_second() * 1000; };

//============================================================
//  renderer_nogpu::create
//============================================================

int renderer_nogpu::create()
{
	// fill in the bitmap info header
	m_bminfo.bmiHeader.biSize            = sizeof(m_bminfo.bmiHeader);
	m_bminfo.bmiHeader.biPlanes          = 1;
	m_bminfo.bmiHeader.biBitCount        = 32;
	m_bminfo.bmiHeader.biCompression     = BI_RGB;
	m_bminfo.bmiHeader.biSizeImage       = 0;
	m_bminfo.bmiHeader.biXPelsPerMeter   = 0;
	m_bminfo.bmiHeader.biYPelsPerMeter   = 0;
	m_bminfo.bmiHeader.biClrUsed         = 0;
	m_bminfo.bmiHeader.biClrImportant    = 0;

	return 0;
}

//============================================================
//  renderer_nogpu::~renderer_nogpu
//============================================================

renderer_nogpu::~renderer_nogpu()
{
	osd_printf_verbose("nogpu: Sending CMD_CLOSE...");
	cmd_close command;

	nogpu_send_command(&command, sizeof(command));
	osd_printf_verbose("done.\n");
}

//============================================================
//  renderer_nogpu::get_primitives
//============================================================

render_primitive_list *renderer_nogpu::get_primitives()
{
	if (m_width == 0 || m_height == 0)
	{
		osd_dim const dimensions = window().get_size();
		if ((dimensions.width() <= 0) || (dimensions.height() <= 0))
			return nullptr;

		m_width = std::min(dimensions.width(), MAX_BUFFER_WIDTH);
		m_height = std::min(dimensions.height(), MAX_BUFFER_HEIGHT);
	}

	window().target()->set_bounds(m_width, m_height, window().pixel_aspect());
	return &window().target()->get_primitives();
}

//============================================================
//  renderer_nogpu::draw
//============================================================

int renderer_nogpu::draw(const int update)
{
	auto &win = dynamic_cast<win_window_info &>(window());

	// we don't have any special resize behaviors
	if (win.m_resize_state == win_window_info::RESIZE_STATE_PENDING)
		win.m_resize_state = win_window_info::RESIZE_STATE_NORMAL;

	// resize window if required
	static int old_width = 0;
	static int old_height = 0;
	if (old_width != m_width || old_height != m_height)
	{
		old_width = m_width;
		old_height = m_height;

		RECT client_rect;
		RECT window_rect;
		GetClientRect(win.platform_window(), &client_rect);
		GetWindowRect(win.platform_window(), &window_rect);

		int extra_width = (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left);
		int extra_height = (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top);

		SetWindowPos(win.platform_window(), nullptr, 0, 0, m_width + extra_width, m_height + extra_height	, SWP_NOMOVE | SWP_NOZORDER);
	}

	// compute pitch of target
	int const pitch = (m_width + 3) & ~3;

	// make sure our temporary bitmap is big enough
	if ((pitch * m_height * 4) > m_bmsize)
	{
		m_bmsize = pitch * m_height * 4 * 2;
		m_bmdata.reset();
		m_bmdata = std::make_unique<uint8_t []>(m_bmsize);
	}

	// draw the primitives to the bitmap
	win.m_primlist->acquire_lock();
	software_renderer<uint32_t, 0,0,0, 16,8,0>::draw_primitives(*win.m_primlist, m_bmdata.get(), m_width, m_height, pitch);
	win.m_primlist->release_lock();

	// fill in bitmap-specific info
	m_bminfo.bmiHeader.biWidth = pitch;
	m_bminfo.bmiHeader.biHeight = -m_height;

	// blit to the screen
	StretchDIBits(
			win.m_dc, 0, 0, m_width, m_height,
			0, 0, m_width, m_height,
			m_bmdata.get(), &m_bminfo, DIB_RGB_COLORS, SRCCOPY);

	// initialize nogpu right before first blit
	if (m_first_blit && !m_initialized)
	{
		m_initialized = nogpu_init();
		if (m_initialized)
			osd_printf_verbose("done.\n");
	}

	// only send frame if nogpu is initialized
	if (!m_initialized)
		return 0;

	// convert RGBA buffer to RGB
	int i = 0, j = 0, k = 0;
	int lstart = m_current_mode.interlace? pitch * 4 * ((m_status.vcount + 1) % 2) : 0;
	int lend = (m_height - 1) * pitch * 4;
	int lstep = pitch * 4 * (m_current_mode.interlace? 2 : 1);
	osd_printf_verbose("lstart %d lend %d lstep %d\n", lstart, lend, lstep);
	for (i = lstart; i <= lend ; i += lstep)
	{
		for (j = 0; j < pitch * 4; j += 4)
		{
			m_fb[k] = (char)m_bmdata[i+j];
			m_fb[k+1] = (char)m_bmdata[i+j+1];
			m_fb[k+2] = (char)m_bmdata[i+j+2];
			k += 3;
		}
	}

	// change video mode right before the blit
	if (m_initialized) nogpu_switch_video_mode();

	bool valid_status = false;

	time_entry = osd_ticks();

	if (video_config.syncrefresh && m_first_blit)
	{
		time_start = time_entry;
		time_blit = time_entry;
		time_exit = time_entry;

		m_status = {};
		valid_status = nogpu_get_status(&m_status, m_period);

		m_first_blit = false;
		m_frame = m_status.frame_num + 1;

		osd_printf_verbose("start: frame %d status_frame %d status_scanline %d\n", m_frame, m_status.frame_num, m_status.vcount);
	}

	// Blit now
	nogpu_blit(m_frame, m_width, m_height / (m_current_mode.interlace? 2 : 1));

	time_blit = osd_ticks();
	osd_printf_verbose("[%.3f] emulation_time: %.3f blit_time: %.3f \n", get_ms(time_entry - time_start), get_ms(time_entry - time_exit), get_ms(time_blit - time_entry));

	// Wait raster position
	if (video_config.syncrefresh)
		valid_status = nogpu_wait_status(&m_status, std::max(0.0d, m_period - get_ms(time_blit - time_exit)));

	time_exit = osd_ticks();

	if (video_config.syncrefresh && valid_status)
	{
		osd_printf_verbose("[%.3f] frame %d status_frame %d status_scanline %d wait_time: %.3f\n\n", get_ms(time_exit - time_start), m_frame, m_status.frame_num, m_status.vcount, get_ms(time_exit - time_blit));
		m_frame = m_status.frame_num + 1;
	}
	else
	{
		osd_printf_verbose("[%.3f] frame %d wait_time: %.3f\n\n", get_ms(time_exit - time_start), m_frame, get_ms(time_exit - time_blit));
		m_frame ++;
	}

	return 0;
}

//============================================================
//  renderer_nogpu::nogpu_init
//============================================================

bool renderer_nogpu::nogpu_init()
{
	WSADATA wsa;
	int result;

	osd_printf_verbose("nogpu: Initializing Winsock...");
	result = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (result != NO_ERROR)
	{
		osd_printf_verbose("Failed. Error code : %d", WSAGetLastError());
		return false;
	}
	osd_printf_verbose("done.\n");

	osd_printf_verbose("nogpu: Initializing socket overlapped...");
	m_sockfd = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (m_sockfd == INVALID_SOCKET)
	{
		osd_printf_verbose("Could not create socket : %d", WSAGetLastError());
		return false;
	}
	osd_printf_verbose("done.\n");

	short port = UDP_PORT;
	const char* local_host = downcast<windows_options &>(window().machine().options()).nogpu_ip();
	m_server_addr.sin_family = AF_INET;
	m_server_addr.sin_port = htons(port);
	m_server_addr.sin_addr.s_addr = inet_addr(local_host);

	// Create a event handlers
	SecureZeroMemory((void *)&m_overlapped, sizeof (WSAOVERLAPPED));
	SecureZeroMemory((void *)&m_overlapped_recv, sizeof (WSAOVERLAPPED));
	m_overlapped.hEvent = WSACreateEvent();
	m_overlapped_recv.hEvent = WSACreateEvent();

	if (m_overlapped.hEvent == NULL || m_overlapped_recv.hEvent == NULL)
	{
		osd_printf_verbose("nogpu: WSACreateEvent failed with error: %d\n", WSAGetLastError());
		return false;
	}

	osd_printf_verbose("nogpu: Setting send buffer to 2097152 bytes...\n");
	int opt_val = 2097152;
	result = setsockopt(m_sockfd, SOL_SOCKET, SO_SNDBUF, (char*)&opt_val, sizeof(opt_val));
	if (result != 0)
	{
		osd_printf_verbose("Unable to set send buffer: %d\n", result);
		return false;
	}

	m_compression = 0; // raw
	const char* compression = downcast<windows_options &>(window().machine().options()).nogpu_compression();
	if (!strcmp(compression, "lz4"))
	{
		m_compression = 0x01;
		osd_printf_verbose("nogpu: compression algorithm %s\n", compression);
	}
	else if (strcmp(compression, "none"))
		osd_printf_verbose("nogpu: compression algorithm %s not supported\n", compression);

	osd_printf_verbose("nogpu: Sending CMD_INIT...");
	cmd_init command;
	command.compression = m_compression;
	command.block_size = m_compression? 8192 : 0;

	// Reset current mode
	m_current_mode = {};

	return nogpu_send_command(&command, sizeof(command));
}

//============================================================
//  renderer_nogpu::nogpu_switch_video_mode()
//============================================================

bool renderer_nogpu::nogpu_switch_video_mode()
{
	// Check if we have a pending mode change
	switchres_manager *m_switchres = &downcast<windows_osd_interface&>(window().machine().osd()).switchres()->switchres();
	if (m_switchres->display(window().index()) == nullptr)
		return false;

	modeline *mode = m_switchres->display(window().index())->selected_mode();
	if (mode == nullptr)
		return false;

	// If not, we're done
	if (!modeline_is_different(mode, &m_current_mode))
		return true;

	m_current_mode = *mode;

	// Send new modeline to nogpu
	osd_printf_verbose("nogpu: Sending CMD_SWITCHRES...\n");

	cmd_switchres command;
	nogpu_modeline *m = &command.mode;

	m->pclock    = double(mode->pclock) / 1000000.0;
	m->hactive   = mode->hactive;
	m->hbegin    = mode->hbegin;
	m->hend      = mode->hend;
	m->htotal    = mode->htotal;
	m->vactive   = mode->vactive;
	m->vbegin    = mode->vbegin;
	m->vend      = mode->vend;
	m->vtotal    = mode->vtotal;
	m->interlace = mode->interlace;

	m_width = mode->hactive;
	m_height = mode->vactive;
	m_vtotal = mode->vtotal;
	m_period = 1000.0 / (double(mode->pclock) / (mode->htotal * mode->vtotal)) / (mode->interlace? 2: 1);

	return nogpu_send_command(&command, sizeof(command));
}

//============================================================
//  renderer_nogpu::get_status
//============================================================

bool renderer_nogpu::nogpu_get_status(nogpu_status *status, double timeout)
{
	cmd_get_status command;
	nogpu_send_command(&command, sizeof(command));
	return nogpu_wait_status(status, timeout);
}

//============================================================
//  renderer_nogpu::wait_status
//============================================================

bool renderer_nogpu::nogpu_wait_status(nogpu_status *status, double timeout)
{
	m_databuf_recv.len = sizeof(nogpu_status);
	m_databuf_recv.buf = (char *)status;

	int err = 0;
	DWORD Flags = 0;
	DWORD bytes_recv;
	int size_sender = sizeof(m_server_addr);
	int retries = 0;
	osd_ticks_t time_1 = 0;
	osd_ticks_t time_2 = 0;

	osd_printf_verbose("[%.3f] wait_status[%.3f]: frame %d ", get_ms(osd_ticks() - time_start), timeout, m_frame);
	time_1 = osd_ticks();

	int rc = WSARecvFrom(m_sockfd, &m_databuf_recv, 1, &bytes_recv, &Flags, (SOCKADDR*)&m_server_addr, &size_sender, &m_overlapped_recv, nullptr);

	// We didn't get a response immediately, wait for it until timeout is reached
	if (rc != 0)
	{
		if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING != (err = WSAGetLastError())))
		{
			osd_printf_verbose("error: WSARecvFrom wait status failed with error: %d\n", err);
			return false;
		}

	retry:

		WSAEVENT event = WSACreateEvent();

		rc = WSAWaitForMultipleEvents(1, &event, FALSE, 1, TRUE);

		WSACloseEvent(event);

		if (rc == WSA_WAIT_FAILED)
		{
			osd_printf_verbose("error: WSAWaitForMultipleEvents wait status failed with error: %d\n"), WSAGetLastError();
			return false;
		}

		retries++;
		time_2 = osd_ticks();

		if (status->frame_num < m_frame)
		{
			if (get_ms(time_2 - time_1) < timeout)
				goto retry;
			else
			{
				osd_printf_verbose("timeout: %.3f retries %d\n", timeout, retries);
				return false;
			}
		}
	}
	// We got an immediate response, just check the clock here
	else
		time_2 = osd_ticks();

	osd_printf_verbose("success: frame %d vcount %d wait %.3f retries %d\n", status->frame_num, status->vcount, get_ms(time_2 - time_1), retries);
	return true;
}

//============================================================
//  renderer_nogpu::nogpu_send_mtu
//============================================================

void renderer_nogpu::nogpu_send_mtu(char *buffer, int bytes_to_send, int chunk_max_size)
{
	int bytes_this_chunk = 0;
	int chunk_size = 0;
	uint32_t offset = 0;

	do
	{
		chunk_size = bytes_to_send > chunk_max_size? chunk_max_size : bytes_to_send;
		bytes_to_send -= chunk_size;
		bytes_this_chunk = chunk_size;

		nogpu_send_command(buffer + offset, bytes_this_chunk);
		offset += chunk_size;

	} while (bytes_to_send > 0);
}

//============================================================
//  renderer_nogpu::nogpu_send_lz4
//============================================================

void renderer_nogpu::nogpu_send_lz4(char *buffer, int bytes_to_send, int block_size)
{
	LZ4_stream_t lz4_stream_body;
	LZ4_stream_t* lz4_stream = &lz4_stream_body;
	LZ4_initStream(lz4_stream, sizeof(*lz4_stream));

	int inp_buf_index = 0;
	int bytes_this_chunk = 0;
	int chunk_size = 0;
	uint32_t offset = 0;

	do
	{
		chunk_size = bytes_to_send > block_size? block_size : bytes_to_send;
		bytes_to_send -= chunk_size;
		bytes_this_chunk = chunk_size;

		char* const inp_ptr = inp_buf[inp_buf_index];
		memcpy((char *)&inp_ptr[0], buffer + offset, chunk_size);

		const uint16_t c_size = LZ4_compress_fast_continue(lz4_stream, inp_ptr, (char *)&m_fb_compressed[2], bytes_this_chunk, sizeof(m_fb_compressed), 1);
		uint16_t *c_size_ptr = (uint16_t *)&m_fb_compressed[0];
		*c_size_ptr = c_size;

		nogpu_send_mtu((char *) &m_fb_compressed[0], c_size + 2, 1472);
		offset += chunk_size;
		inp_buf_index ^= 1;

	} while (bytes_to_send > 0);
}

//============================================================
//  renderer_nogpu::nogpu_blit
//============================================================

void renderer_nogpu::nogpu_blit(uint32_t frame, uint16_t width, uint16_t height)
{
	// Compressed blocks are 16 lines long
	int block_size = m_compression? (width << 4) * 3 : 0;

	// We need to make sure we don't blit until VRAM is empty
	//int min_sync_line = std::max(height - VRAM_BUFFER_SIZE / width, 0);
	int min_sync_line = 0;

	// Update vsync scanline
	m_vsync_scanline = (height - min_sync_line) * ((float)(video_config.framedelay) / 10) + min_sync_line + 1;

	// Send CMD_BLIT
	cmd_blit command;
	command.frame = frame;
	command.vsync = video_config.syncrefresh? m_vsync_scanline : 0;
	command.block_size = block_size;
	nogpu_send_command(&command, sizeof(command));

	if (m_compression == 0)
		nogpu_send_mtu(&m_fb[0], width * height * 3, 1470);

	else
		nogpu_send_lz4(&m_fb[0], width * height * 3, block_size);
}

//============================================================
//  renderer_nogpu::nogpu_send_command
//============================================================

bool renderer_nogpu::nogpu_send_command(void *command, int command_size)
{
	int err = 0, rc;
	DWORD bytes_sent, flags;

	m_databuf.len = command_size;
	m_databuf.buf = (char *)command;

	rc = WSASendTo(m_sockfd, &m_databuf, 1, &bytes_sent, 0, (SOCKADDR*)&m_server_addr, sizeof(m_server_addr), &m_overlapped, NULL);
	if (rc != 0)
	{
		if (rc == SOCKET_ERROR && WSA_IO_PENDING != (err = WSAGetLastError()))
		{
			osd_printf_verbose("nogpu_send_command: WSASend failed with error: %d\n", err);
			return false;
		}

		rc = WSAWaitForMultipleEvents(1, &m_overlapped.hEvent, true, WSA_INFINITE, false);
		WSAResetEvent(m_overlapped.hEvent);

		if (rc == WSA_WAIT_FAILED)
		{
			osd_printf_verbose("nogpu_send_command: WSAWaitForMultipleEvents failed with error: %d\n", WSAGetLastError());
			return false;
		}

		if (false == WSAGetOverlappedResult(m_sockfd, &m_overlapped, &bytes_sent, false, &flags))
		{
			osd_printf_verbose("nogpu_send_command: WSAGetOverlapped init failed with error: %d\n", WSAGetLastError());
			return false;
		}
	}
	return true;
}

//

class video_nogpu : public osd_module, public render_module
{
public:
	video_nogpu() : osd_module(OSD_RENDERER_PROVIDER, "nogpu") { }

	virtual int init(osd_interface &osd, osd_options const &options) override { return 0; }
	virtual void exit() override { }

	virtual std::unique_ptr<osd_renderer> create(osd_window &window) override;

protected:
	virtual unsigned flags() const override { return FLAG_INTERACTIVE; }
};

std::unique_ptr<osd_renderer> video_nogpu::create(osd_window &window)
{
	return std::make_unique<renderer_nogpu>(window);
}

} // anonymous namespace

} // namespace osd

MODULE_DEFINITION(RENDERER_NOGPU, osd::video_nogpu)

