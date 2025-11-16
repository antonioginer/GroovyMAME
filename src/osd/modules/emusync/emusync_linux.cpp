// license:BSD-3-Clause
// copyright-holders:Antonio Giner
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

static int drm_open(const char *dri_device, int monitor_handle);
static int fd = 0;
static int crtc_id = 0;
static bool must_close_fd = false;


//============================================================
//  emusync:init_osd
//============================================================

bool emusync::osd_init(uint64_t monitor_handle, std::function<bool(void)> get_vblank_timestamp_external, std::function<uint64_t(void)> get_frame_counter_external)
{
	get_vblank_timestamp = get_vblank_timestamp_external == nullptr ?
						std::bind(&emusync::get_vblank_timestamp_default, this) :
						get_vblank_timestamp_external;

	get_frame_counter = get_frame_counter_external;

	m_count_div = dynamic_cast<sdl_options const &>(machine().options()).interlace_force_even();

	display_manager *display = downcast<sdl_osd_interface&>(machine().osd()).switchres()->switchres().display(0);
	if (display != nullptr)
	{
		int *sr_fd = (int*)display->video()->get_resource(SR_RES_KMS_FD);
		if (sr_fd) fd = *sr_fd;

		int *sr_crtc_id = (int*)display->video()->get_resource(SR_RES_KMS_CRTC_ID);
		if (sr_crtc_id) crtc_id = *sr_crtc_id;
	}

	if (fd == 0)
	{
		fd = drm_open(dynamic_cast<sdl_options const &>(machine().options()).dri_device(), (int)monitor_handle);
		if (fd)
			must_close_fd = true;
	}

	return (fd != 0);
}


//============================================================
//  emusync:osd_deinit
//============================================================

void emusync::osd_deinit()
{
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

	// Sync counts increase by 2 on interlaced modes. Normalize
	if (interlaced())
		sequence = sequence >> m_count_div;

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
