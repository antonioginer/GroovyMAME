// DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>

static int drm_open(const char *dri_device);
static int drm_get_crtc(int fd, int crtc);
//static void drm_waitvblank(int crtc);
static int fd = 0;
static int crtc_id = 0;
static const char* dri_device = nullptr;


//============================================================
//  emusync:init_osd
//============================================================

void emusync::osd_init(uint64_t monitor_handle, std::function<bool(void)> get_vblank_timestamp_external, std::function<uint64_t(void)> get_frame_counter_external)
{
	get_vblank_timestamp = get_vblank_timestamp_external == nullptr ?
						std::bind(&emusync::get_vblank_timestamp_default, this) :
						get_vblank_timestamp_external;

	get_frame_counter = get_frame_counter_external;

	scanline_init(monitor_handle, use_polling_thread);
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

	m_sync.register_vblank_in_ns(sequence, ns);

	return true;
}


//============================================================
//  drm_open
//============================================================

static int drm_open(const char *dri_device)
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


//============================================================
//  drm_get_crtc
//============================================================

int drm_get_crtc(int fd, int crtc)
{
	drmModeRes *resources = drmModeGetResources(fd);
	int crtc_id = 0;

	if (!resources)
	{
		printf("drm_get_crtc_id: couldn't find resources.\n");
		return 0;
	}

	if (resources->count_crtcs < 1)
	{
		printf("drm_get_crtc_id: couldn't find crtcs.\n");
		drmModeFreeResources(resources);
		return 0;
	}

	if (crtc > resources->count_crtcs)
	{
		printf("drm_get_crtc_id: crtc %d not found.\n", crtc);
		drmModeFreeResources(resources);
		return 0;
	}

	crtc_id = resources->crtcs[crtc];
	drmModeFreeResources(resources);

	return crtc_id;
}


//============================================================
//  drm_waitvblank
//============================================================

static void drm_waitvblank(int crtc)
{

	drmVBlank vbl;
	memset(&vbl, 0, sizeof(vbl));
	vbl.request.sequence = 1;

	// handle vblank for all SR managed crtc
	// this is a hack based on SDL reported screen index
	// it won't work on multi-gpu
	// TO DO: find a correct way to map screen to crtc

	// single screen (default)
	vbl.request.type = DRM_VBLANK_RELATIVE;

	// two screens
	if (crtc == 1) vbl.request.type = drmVBlankSeqType(DRM_VBLANK_RELATIVE | DRM_VBLANK_SECONDARY);

	// multi-screen
	else if (crtc > 1)
	{
		static uint64_t caps;
		static bool caps_checked = false;

		if (!caps_checked)
		{
			caps_checked = true;
			if (drmGetCap(fd, DRM_CAP_VBLANK_HIGH_CRTC, &caps))
				osd_printf_error("A newer kernel is needed for vblank syncing on multi screen\n");
		}
		if (caps)
			vbl.request.type = drmVBlankSeqType(DRM_VBLANK_RELATIVE | ((crtc << DRM_VBLANK_HIGH_CRTC_SHIFT) & DRM_VBLANK_HIGH_CRTC_MASK));
	}

	if (drmWaitVBlank(fd, &vbl) != 0)
		osd_printf_verbose("drmWaitVBlank failed\n");
}

