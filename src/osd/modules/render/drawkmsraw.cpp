// license:BSD-3-Clause
// copyright-holders:Antonio Giner
//============================================================
//
//  drawkmsraw.cpp - KMSDRM software front buffer rendering
//
//============================================================

#include "render_module.h"

#include "modules/osdmodule.h"

#if defined(OSD_SDL)

#include "window.h"
#include "osdsdl.h"

// emu
#include "emu.h"
#include "rendersw.hxx"
#include <switchres/switchres.h>
#include <switchres/switchres_defines.h>

#include <xf86drmMode.h>
#include <xf86drm.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>


namespace osd {

namespace {

static int drm_open(const char *dri_device, int monitor_handle);
static int drm_get_crtc(int fd, int crtc);
static int fd = 0;
static int crtc_id = 0;

// renderer_kmsraw is the information for the current screen
class renderer_kmsraw : public osd_renderer
{
public:
	renderer_kmsraw(osd_window &window)
		: osd_renderer(window)
		, m_bmdata(nullptr)
		, m_bmsize(0)
	{
	}

	virtual int create() override;
	virtual render_primitive_list *get_primitives() override;
	virtual int draw(const int update) override;
	virtual void save() override {}
	virtual void record() override {}
	virtual void toggle_fsfx() override {}

private:
	std::unique_ptr<uint8_t []> m_bmdata;
	size_t                      m_bmsize;
};

//============================================================
//  renderer_kmsraw::create
//============================================================

int renderer_kmsraw::create()
{
	switchres_manager *m_switchres = &downcast<sdl_osd_interface&>(window().machine().osd()).switchres()->switchres();
	display_manager *display = m_switchres->display(window().index());

	void *map = display->video()->get_resource(SR_RES_KMS_BUFFER);
	if (map == nullptr)
		osd_printf_error("no buffer found\n");

	memset(map, 80, 320*240*4);

	return 0;

	int err;
	fd = drm_open("auto", 0);

	drmModeCrtc *crtc = drmModeGetCrtc(fd, 68);
	if (!crtc)
	{
		osd_printf_error("drmModeGetCrtc failed\n");
		return -1;
	}

	uint32_t fb_id = crtc->buffer_id;
	//drmModeFreeCrtc(crtc);

	osd_printf_info("renderer_kmsraw::create success, fd: %d crtc_id: %d buffer_id: %d\n", fd, crtc_id, fb_id);

	drmModeFB2 *fb2 = drmModeGetFB2(fd, fb_id);
	if (fb2)
	{
		osd_printf_info("FB2 handles:\n");
		for (int i = 0; i < 4; ++i)
		{
			if (fb2->handles[i])
				osd_printf_info("  plane %d -> handle = %u\n", i, fb2->handles[i]);
		}
		drmModeFreeFB2(fb2);
		return 0;
	}

	drmModeFB *fb = drmModeGetFB(fd, fb_id);
	if (!fb) goto cleanup;

	osd_printf_info("legacy FB -> handle = %u (fb id = %u)\n", fb->handle, fb->fb_id);

	struct drm_mode_map_dumb mreq;

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = fb->handle;

	err = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (err)
	{
		osd_printf_error("Mode map dumb framebuffer failed (err=%d)\n", err);
		goto cleanup;
	}

	uint8_t *data;
	data = (uint8_t*) mmap(0, 3840*2160*4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);
	if (data == MAP_FAILED)
	{
		err = errno;
		osd_printf_error("Mode map failed (err=%d)\n", err);
		goto cleanup;
	}

	cleanup:
	drmModeFreeFB(fb);
	return -1;
}

//============================================================
//  renderer_kmsraw::get_primitives
//============================================================

render_primitive_list *renderer_kmsraw::get_primitives()
{
	osd_dim const dimensions = window().get_size();
	if ((dimensions.width() <= 0) || (dimensions.height() <= 0))
		return nullptr;

	window().target()->set_bounds(dimensions.width(), dimensions.height(), window().pixel_aspect());
	return &window().target()->get_primitives();
}

//============================================================
//  renderer_kmsraw::draw
//============================================================

int renderer_kmsraw::draw(const int update)
{
	auto &win = dynamic_cast<sdl_window_info &>(window());

	// compute width/height/pitch of target
	osd_dim const dimensions = win.get_size();
	int const width = dimensions.width();
	int const height = dimensions.height();
	int const pitch = (width + 3) & ~3;

	// make sure our temporary bitmap is big enough
	if ((pitch * height * 4) > m_bmsize)
	{
		m_bmsize = pitch * height * 4 * 2;
		m_bmdata.reset();
		m_bmdata = std::make_unique<uint8_t []>(m_bmsize);
	}

	// draw the primitives to the bitmap
	win.m_primlist->acquire_lock();
	software_renderer<uint32_t, 0,0,0, 16,8,0>::draw_primitives(*win.m_primlist, m_bmdata.get(), width, height, pitch);
	win.m_primlist->release_lock();

	return 0;
}


class video_kmsraw : public osd_module, public render_module
{
public:
	video_kmsraw() : osd_module(OSD_RENDERER_PROVIDER, "kmsraw") { }

	virtual int init(osd_interface &osd, osd_options const &options) override { return 0; }
	virtual void exit() override { }

	virtual std::unique_ptr<osd_renderer> create(osd_window &window) override;

protected:
	virtual unsigned flags() const override { return FLAG_INTERACTIVE; }
};

std::unique_ptr<osd_renderer> video_kmsraw::create(osd_window &window)
{
	return std::make_unique<renderer_kmsraw>(window);
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

	crtc_id = drm_get_crtc(fd, monitor_handle);

	osd_printf_verbose("drm_open: %s successfully opened\n", node);
	return fd;
}

//============================================================
//  drm_get_crtc
//============================================================

static int drm_get_crtc(int fd, int crtc)
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


} // anonymous namespace

} // namespace osd


#else // defined(OSD_SDL)

namespace osd { namespace { MODULE_NOT_SUPPORTED(video_kmsraw, OSD_RENDERER_PROVIDER, "kmsraw") } }

#endif // defined(OSD_WINDOWS)


MODULE_DEFINITION(RENDERER_KMSRAW, osd::video_kmsraw)
