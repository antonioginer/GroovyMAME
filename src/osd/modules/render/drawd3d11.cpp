// license:BSD-3-Clause
// copyright-holders:Antonio Giner
//============================================================
//
//  drawd3d11.cpp - Win32 Direct3D 11 implementation
//
//============================================================

// MAME headers
#include "emu.h"
#include "emuopts.h"
#include "render.h"
#include "rendutil.h"
#include "screen.h"

#include "modules/lib/osdlib.h"
//#include "modules/osdwindow.h"
#include "window.h"
#include "render_module.h"

#include <d3d11.h>
#include <wrl/client.h>

/* renderer_d3d11 is the information about Direct3D 11 for the current screen */
class renderer_d3d11 : public osd_renderer
{
public:

	renderer_d3d11(osd_window &window, ID3D11Device *d3d11_device, IDXGIFactory1 *dxgi_factory);
	virtual ~renderer_d3d11() {};

	virtual int create() override;
	virtual render_primitive_list *get_primitives() override { return nullptr; };
	virtual int draw(const int update) override { return 0; };
/*
	virtual void save() override {};
	virtual void record() override {};
	virtual void toggle_fsfx() override {};
	virtual void add_audio_to_recording(const int16_t *buffer, int samples_this_frame) override {};
	virtual std::vector<ui::menu_item> get_slider_list() override { return {}; };
	virtual int restart() override { return 0; };
*/
private:
	ID3D11Device*           m_d3d11_device;             // Direct3D 11 device
	IDXGIFactory1*          m_dxgi_factory;             // Direct3D 11 device
	int                     m_adapter;                  // ordinal adapter number
	int                     m_vendor_id;                // adapter vendor id
	int                     m_width;                    // current width
	int                     m_height;                   // current height
	int                     m_refresh;                  // current refresh rate
	bool                    m_interlace;                // current interlace
	int                     m_frame_delay;              // current frame delay value
};

renderer_d3d11::renderer_d3d11(osd_window &window, ID3D11Device *d3d11_device, IDXGIFactory1 *dxgi_factory)
	: osd_renderer(window)
	, m_d3d11_device(d3d11_device)
	, m_dxgi_factory(dxgi_factory)
	, m_adapter(0)
	, m_width(0)
	, m_height(0)
	, m_refresh(0)
	, m_frame_delay(0)
{
}

int renderer_d3d11::create()
{
	HWND hwnd = dynamic_cast<win_window_info &>(window()).platform_window();

	// Create swapchain
	DXGI_SWAP_CHAIN_DESC scd;
	IDXGISwapChain *pSwapChain;

	memset(&scd, 0, sizeof(scd));
	scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.SampleDesc.Count = 1;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.BufferCount = 1;
	scd.OutputWindow = hwnd;
	scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	scd.Windowed = TRUE;
	scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	m_dxgi_factory->CreateSwapChain(m_d3d11_device, &scd, &pSwapChain);

	return 0;
}

//============================================================
//  OSD MODULE
//============================================================

namespace osd {

namespace {

class video_d3d11 : public osd_module, public render_module
{
public:
	video_d3d11()
		: osd_module(OSD_RENDERER_PROVIDER, "d3d11")
		, m_options(nullptr)
	{
	}

	virtual bool probe() override;
	virtual int init(osd_interface &osd, osd_options const &options) override;
	virtual void exit() override;

	virtual std::unique_ptr<osd_renderer> create(osd_window &window) override;

protected:
	virtual unsigned flags() const override { return FLAG_INTERACTIVE; }

private:
	using dxgi_create_dxgi_factory_fn = HRESULT *(WINAPI *)(REFIID riid, void **factory);
	dynamic_module::ptr m_d3d11_dll;
	dynamic_module::ptr m_dxgi_dll;
	Microsoft::WRL::ComPtr<ID3D11Device> m_d3d11_device;
	Microsoft::WRL::ComPtr<IDXGIFactory1> m_dxgi_factory;
	osd_options const *m_options;
};


//============================================================
//  video_d3d11::probe
//============================================================

bool video_d3d11::probe()
{
	// do a dry run of loading the Direct3D 11 DLL
	return dynamic_module::open({ "d3d11.dll" })->bind<PFN_D3D11_CREATE_DEVICE>("D3D11CreateDevice") != nullptr;
}


//============================================================
//  video_d3d11::init
//============================================================

int video_d3d11::init(osd_interface &osd, osd_options const &options)
{
	m_options = &options;

	m_d3d11_dll = dynamic_module::open({ "d3d11.dll" });
	auto const d3d11_create_device = m_d3d11_dll->bind<PFN_D3D11_CREATE_DEVICE>("D3D11CreateDevice");
	if (!d3d11_create_device)
	{
		osd_printf_warning("Direct3D: Could not find D3D11CreateDevice function in d3d11.dll\n");
		m_d3d11_dll.reset();
		m_options = nullptr;
		return -1;
	}

	ID3D11DeviceContext *p_device_context;

	(*d3d11_create_device)(
		NULL,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		D3D11_CREATE_DEVICE_DEBUG,
		NULL,
		0,
		D3D11_SDK_VERSION,
		&m_d3d11_device,
		NULL,
		&p_device_context);

	if (!m_d3d11_device)
	{
		osd_printf_warning("Direct3D: Unable to initialize Direct3D 11\n");
		m_d3d11_dll.reset();
		m_options = nullptr;
		return -1;
	}

	m_dxgi_dll = dynamic_module::open({ "dxgi.dll" });
	auto const dxgi_create_dxgi_factory = m_dxgi_dll->bind<dxgi_create_dxgi_factory_fn>("CreateDXGIFactory1");
	if (!dxgi_create_dxgi_factory)
	{
		osd_printf_warning("Direct3D: Could not find CreateDXGIFactory1 function in dxgi.dll\n");
		m_dxgi_dll.reset();
		m_options = nullptr;
		return -1;
	}

	(*dxgi_create_dxgi_factory)(__uuidof(IDXGIFactory1), &m_dxgi_factory);

	osd_printf_verbose("Direct3D: Using Direct3D 11\n");

	return 0;
}


//============================================================
//  video_d3d11::exit
//============================================================

void video_d3d11::exit()
{
	m_d3d11_device.Reset();
	m_dxgi_factory.Reset();
	m_d3d11_dll.reset();
	m_dxgi_dll.reset();
	m_options = nullptr;
}


//============================================================
//  video_d3d11::create
//============================================================

std::unique_ptr<osd_renderer> video_d3d11::create(osd_window &window)
{
	return std::make_unique<renderer_d3d11>(window, m_d3d11_device.Get(), m_dxgi_factory.Get());
}

} // anonymous namespace

} // namespace osd

MODULE_DEFINITION(RENDERER_D3D11, osd::video_d3d11)
