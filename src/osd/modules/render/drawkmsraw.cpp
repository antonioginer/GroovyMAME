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
#include "modules/monitor/monitor_common.h"
#include "emusync.h"


namespace osd {

namespace {

// renderer_kmsraw is the information for the current screen
class renderer_kmsraw : public osd_renderer
{
public:
	renderer_kmsraw(osd_window &window)
		: osd_renderer(window)
		, m_bmdata(nullptr)
		, m_bmsize(0)
		, m_sync(window.machine().sync())
	{
	}
	~renderer_kmsraw()
	{
		// destroy vblank thread
		m_sync.osd_deinit();
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

	switchres_manager *m_switchres;
	display_manager   *m_display;

	// emusync manager
	emusync         &m_sync;
};


//============================================================
//  renderer_kmsraw::create
//============================================================

int renderer_kmsraw::create()
{
	m_switchres = &downcast<sdl_osd_interface&>(window().machine().osd()).switchres()->switchres();
	if (m_switchres == nullptr)
		return -1;

	m_display = m_switchres->display(window().index());
	if (m_display == nullptr)
		return -1;

	void *map = m_display->video()->get_resource(SR_RES_KMS_BUFFER);
	if (map == nullptr)
	{
		osd_printf_error("kmsraw: no buffer found!\n");
		return -1;
	}

	if (window().index() == 0 && window().machine().sync().sync_refresh())
		m_sync.osd_init(window().monitor()->oshandle(), nullptr, nullptr);

	return 0;
}


//============================================================
//  renderer_kmsraw::get_primitives
//============================================================

render_primitive_list *renderer_kmsraw::get_primitives()
{
	if ((m_display->width() <= 0) || (m_display->height() <= 0))
		return nullptr;

	float pixel_aspect = m_display->monitor_aspect() / ((float)m_display->width() / m_display->height());

	window().target()->set_bounds(m_display->width(), m_display->height(), pixel_aspect);
	return &window().target()->get_primitives();
}

//============================================================
//  renderer_kmsraw::draw
//============================================================

int renderer_kmsraw::draw(const int update)
{
	auto &win = dynamic_cast<sdl_window_info &>(window());

	// compute width/height/pitch of target
	int const width = m_display->width();
	int const height = m_display->height();
	int *kms_pitch = (int*)m_display->video()->get_resource(SR_RES_KMS_PITCH);
	int const pitch = *kms_pitch / 4;

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

	// get to dumb buffer
	void *map = m_display->video()->get_resource(SR_RES_KMS_BUFFER);
	if (map == nullptr)
	{
		osd_printf_error("kmsraw: no buffer found!\n");
		return -1;
	}

	m_sync.register_tag(emusync::BEFORE_DRAW);

	m_sync.predraw_sync();

	m_sync.register_tag(emusync::BEFORE_PRESENT);

	// blit frame
	memcpy(map, m_bmdata.get(), pitch * height * 4);

	m_sync.register_tag(emusync::AFTER_PRESENT);

	m_sync.postdraw_sync();

	m_sync.register_tag(emusync::AFTER_DRAW);

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

} // anonymous namespace

} // namespace osd


#else // defined(OSD_SDL)

namespace osd { namespace { MODULE_NOT_SUPPORTED(video_kmsraw, OSD_RENDERER_PROVIDER, "kmsraw") } }

#endif // defined(OSD_WINDOWS)


MODULE_DEFINITION(RENDERER_KMSRAW, osd::video_kmsraw)
