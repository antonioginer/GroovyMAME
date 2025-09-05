// license:BSD-3-Clause
// copyright-holders: Antonio Giner, intealls
/***************************************************************************

    drm_vbl.cpp

    DRM vblank handling

***************************************************************************/

#include "drm_vbl.h"

#ifdef SDLMAME_X11

drm_vblank_handler::drm_vblank_handler(const char* device, bool use_thread)
	: dri_fd(0)
	, dri_device(device)
	, use_vbl_thread(use_thread)
	, init_vbl_thread(true)
	, kill_vbl_thread(false)
	, m_vblthread(nullptr)
	, vbl_queue(nullptr)
{
	if (use_thread)
		vbl_queue = new blocking_queue<u64>(3);

	dri_fd = drm_open(dri_device);
}

bool drm_vblank_handler::is_open()
{
	return fcntl(dri_fd, F_GETFD) != EINVAL;
}

drm_vblank_handler::~drm_vblank_handler()
{
	if (use_vbl_thread)
	{
		kill_vbl_thread = true;
		m_vblthread->join();

		kill_vbl_thread = false;
		init_vbl_thread = true;

		delete m_vblthread;
		m_vblthread = nullptr;

		delete vbl_queue;
	}

	if (is_open())
		close(dri_fd);
}

int drm_vblank_handler::drm_open(const char *dri_device)
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
				for (int j = 0; j < resources->count_connectors; j++)
				{
					drmModeConnector *conn = drmModeGetConnector(fd, resources->connectors[j]);
					if (!conn) continue;

					// We found a valid connector, use it
					if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
						found = true;

					drmModeFreeConnector(conn);
					if (found) break;
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

void drm_vblank_handler::set_up_drmvbl_request(drmVBlank* vbl, int crtc, int single_force_crtc)
{
	memset(vbl, 0, sizeof(*vbl));
	vbl->request.sequence = 1;

	// handle vblank for all SR managed crtc
	// this is a hack based on SDL reported screen index
	// it won't work on multi-gpu
	// TO DO: find a correct way to map screen to crtc

	// single screen (default)
	vbl->request.type = (drmVBlankSeqType)(DRM_VBLANK_RELATIVE | ((single_force_crtc << DRM_VBLANK_HIGH_CRTC_SHIFT) & DRM_VBLANK_HIGH_CRTC_MASK));

	// two screens
	if (crtc == 1) vbl->request.type = drmVBlankSeqType(DRM_VBLANK_RELATIVE | DRM_VBLANK_SECONDARY);

	// multi-screen
	else if (crtc > 1)
	{
		static uint64_t caps;
		static bool caps_checked = false;

		if (!caps_checked)
		{
			caps_checked = true;
			if (drmGetCap(dri_fd, DRM_CAP_VBLANK_HIGH_CRTC, &caps))
				osd_printf_error("A newer kernel is needed for vblank syncing on multi screen\n");
		}
		if (caps)
			vbl->request.type = drmVBlankSeqType(DRM_VBLANK_RELATIVE | ((crtc << DRM_VBLANK_HIGH_CRTC_SHIFT) & DRM_VBLANK_HIGH_CRTC_MASK));
	}
}

void drm_vblank_handler::vbl_thread_func(const int crtc, const int single_force_crtc)
{
	drmVBlank vbl;

	while (!kill_vbl_thread) {
		set_up_drmvbl_request(&vbl, crtc, single_force_crtc);

		if (drmWaitVBlank(dri_fd, &vbl) != 0)
			osd_printf_verbose("drmWaitVBlank failed\n");

		if (!vbl_queue->full())
			vbl_queue->push(vbl.reply.tval_sec * 1e6 + vbl.reply.tval_usec);
	}

	if (!vbl_queue->full())
		vbl_queue->push(0);
}

void drm_vblank_handler::drm_waitvblank(int crtc, int single_force_crtc)
{
	drmVBlank vbl;

	if (!use_vbl_thread)
		set_up_drmvbl_request(&vbl, crtc, single_force_crtc);

	if (use_vbl_thread && init_vbl_thread) {
		m_vblthread = new std::thread([this, crtc, single_force_crtc]() { vbl_thread_func(crtc, single_force_crtc); });
		init_vbl_thread = false;
	}

	if (use_vbl_thread && !kill_vbl_thread) {
		size_t items = std::max<int>(vbl_queue->count(), 1);

		for (size_t i = 0; i < items; i++)
			vbl_queue->pop();
	} else if (!kill_vbl_thread) {
		if (drmWaitVBlank(dri_fd, &vbl) != 0)
			osd_printf_verbose("drmWaitVBlank failed\n");
	}
}
#endif
