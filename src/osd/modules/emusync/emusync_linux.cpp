// license:BSD-3-Clause
// copyright-holders:Antonio Giner, intealls
//============================================================
//
//  emusync_linux.cpp - Linux raster synchronization
//
//============================================================


// DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <unistd.h>

#include "osdsdl.h"

#include "emu.h"
#include "emuopts.h"
#include "emusync.h"

#include <switchres/switchres.h>
#include <switchres/switchres_defines.h>

#include <atomic>
#include <thread>
#include <mutex>

using namespace osd;

class drm_vblank_handler {
public:
	drm_vblank_handler(emusync& sync, int fd, int crtc);
	~drm_vblank_handler();
	bool get_vblank_timestamp();
private:
	emusync& m_sync;
	int m_dri_fd;

	std::atomic<uint64_t> m_sequence;
	std::atomic<uint64_t> m_ns;

	std::atomic<bool> m_thread_is_active;
	std::thread m_thread;
	std::mutex m_mutex;

	void vbl_thread_func(const int crtc);
};


//============================================================
//  drm_vblank_handler::drm_vblank_handler
//============================================================

drm_vblank_handler::drm_vblank_handler(emusync& sync, int fd, int crtc)
	: m_sync(sync)
	, m_dri_fd(fd)
	, m_sequence(0)
	, m_ns(0)
	, m_thread_is_active(true)
	, m_thread([this, crtc]() { vbl_thread_func(crtc); })
{
}


//============================================================
//  drm_vblank_handler::~drm_vblank_handler
//============================================================

drm_vblank_handler::~drm_vblank_handler()
{
	m_thread_is_active = false;
	m_thread.join();
}


//============================================================
//  drm_vblank_handler::vbl_thread_func
//============================================================

void drm_vblank_handler::vbl_thread_func(const int crtc)
{
	drmVBlank vbl;

	osd_printf_verbose("emusync: polling thread started.\n");

	while (m_thread_is_active)
	{
		struct timespec ts;

		memset(&vbl, 0, sizeof(vbl));

		vbl.request.sequence = 1;
		vbl.request.type = drmVBlankSeqType(DRM_VBLANK_RELATIVE | ((crtc << DRM_VBLANK_HIGH_CRTC_SHIFT) & DRM_VBLANK_HIGH_CRTC_MASK));

		if (drmWaitVBlank(m_dri_fd, &vbl))
		{
			osd_printf_verbose("[%f] drmWaitVBlank failed: %s\n", (double)osd_ticks() / osd_ticks_per_second(), strerror(errno));
			std::this_thread::sleep_for(std::chrono::milliseconds(8));
			continue;
		}

		if (clock_gettime(CLOCK_MONOTONIC, &ts))
		{
			osd_printf_verbose("[%f] clock_gettime failed: %s\n", (double)osd_ticks() / osd_ticks_per_second(), strerror(errno));
			continue;
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);

			m_sequence = vbl.reply.sequence;
			m_ns = ts.tv_sec * 1e9 + ts.tv_nsec;

			//m_sequence = vbl.reply.sequence;
			//m_ns = (vbl.reply.tval_sec * 1e6 + vbl.reply.tval_usec) * 1e3;
		}
	}
	osd_printf_verbose("emusync: polling thread destroyed.\n");
}


//============================================================
//  drm_vblank_handler::get_vblank_timestamp
//============================================================

bool drm_vblank_handler::get_vblank_timestamp()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sync.register_vblank_in_ns(m_sequence, m_ns);
	return true;
}


static int drm_open(const char *dri_device, int monitor_handle);
static int fd = 0;
static int crtc_id = 0;
static int crtc_idx = -1;
static bool must_close_fd = false;
static drm_vblank_handler* drmvbl = nullptr;


//============================================================
//  emusync:init_osd
//============================================================

bool emusync::osd_init(uint64_t monitor_handle, std::function<bool(void)> get_vblank_timestamp_external, std::function<uint64_t(void)> get_frame_counter_external)
{
	display_manager *display = downcast<sdl_osd_interface&>(machine().osd()).switchres()->switchres().display(0);
	if (display != nullptr)
	{
		int *sr_fd = (int*)display->video()->get_resource(SR_RES_KMS_FD);
		if (sr_fd) fd = *sr_fd;

		int *sr_crtc_id = (int*)display->video()->get_resource(SR_RES_KMS_CRTC_ID);
		if (sr_crtc_id) crtc_id = *sr_crtc_id;

		int *sr_crtc_idx = (int*)display->video()->get_resource(SR_RES_KMS_CRTC_IDX);
		if (sr_crtc_idx) crtc_idx = *sr_crtc_idx;
	}

	const sdl_options& options = dynamic_cast<sdl_options const &>(machine().options());

	if (fd == 0)
	{
		fd = drm_open(options.dri_device(), (int)monitor_handle);
		if (fd)
			must_close_fd = true;
	}

	if (fd && options.vblank_thread() && crtc_idx >= 0)
	{
		drmvbl = new drm_vblank_handler(*this, fd, crtc_idx);
		get_vblank_timestamp = std::bind(&drm_vblank_handler::get_vblank_timestamp, drmvbl);
		get_frame_counter = nullptr;
	}
	else
	{
		get_vblank_timestamp = get_vblank_timestamp_external == nullptr ?
							std::bind(&emusync::get_vblank_timestamp_default, this) :
							get_vblank_timestamp_external;

		get_frame_counter = get_frame_counter_external;
	}

	return (fd != 0);
}


//============================================================
//  emusync:osd_deinit
//============================================================

void emusync::osd_deinit()
{
	if (drmvbl != nullptr)
	{
		delete drmvbl;
		drmvbl = nullptr;
	}

	if (must_close_fd)
		close(fd);
}


//============================================================
//  emusync::get_vblank_timestamp_default
//============================================================

bool emusync::get_vblank_timestamp_default()
{
	uint64_t sequence = 0;
	uint64_t ns = 0;

	int ret = drmCrtcGetSequence(fd, crtc_id, &sequence, &ns);
	if (ret != 0)
	{
		osd_printf_verbose("error: drmCrtcGetSequence(%d)\n", ret);
		return false;
	}

	register_vblank_in_ns(sequence, ns);

	return true;
}


//============================================================
//  drm_open
//============================================================

static int drm_open(const char *dri_device, int monitor_handle)
{
	int fd = 0;
	char dri_path[16];
	char *node = dri_path;

	// Dri device forced by user
	if (strcmp(dri_device, "auto") != 0)
	{
		osd_printf_verbose("drm_open: %s for by user\n", dri_device);
		snprintf(node, sizeof(dri_path), "/dev/dri/%s", dri_device);
	}

	// Automatic selection
	else
	{
		// Get an array of drm devices to check
		int num_devices = drmGetDevices2(0, NULL, 0);
		if (num_devices <= 0)
		{
			osd_printf_error("drm_open: couldn't find any drm device\n");
			return 0;
		}

		drmDevicePtr *devices = (drmDevicePtr*)calloc(num_devices, sizeof(drmDevicePtr));
		if (drmGetDevices2(0, devices, num_devices) < 0)
		{
			osd_printf_error("drm_open: drmGetDevices2() failed\n");
			return 0;
		}

		// Parse device list to find the first one with a valid connector
		bool found = false;

		for (int i = 0; i < num_devices; i++)
		{
			int crtc_count = 0;

			// Skip non-primary nodes
			if (devices[i]->available_nodes & (1 << DRM_NODE_PRIMARY))
				node = devices[i]->nodes[DRM_NODE_PRIMARY];

			else continue;

			fd = open(node, O_RDWR | O_CLOEXEC);
			if (fd < 0)
			{
				osd_printf_error("drm_open: couldn't open %s\n", node);
				continue;
			}
			drmModeRes *resources = drmModeGetResources(fd);
			if (resources && resources->count_connectors > 0 && resources->count_encoders > 0 && resources->count_crtcs > 0)
			{
				for (int j = 0; j < resources->count_crtcs; j++)
				{
					if (crtc_count == monitor_handle)
					{
						found = true;
						crtc_id = resources->crtcs[j];
						crtc_idx = j;
						osd_printf_verbose("drm_open: crtc_id: %d\n", crtc_id);
						break;
					}
					crtc_count++;
				}
			}
			drmModeFreeResources(resources);
			close(fd);

			if (found) break;
		}

		drmFreeDevices(devices, num_devices);
		free(devices);

		if (!found)
		{
			osd_printf_error("drm_open: couldn't find any device with a valid connector\n");
			return 0;
		}
	}

	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0)
	{
		osd_printf_error("drm_open: cannot open %s\n", node);
		return 0;
	}

	osd_printf_verbose("drm_open: %s successfully opened\n", node);
	return fd;
}
