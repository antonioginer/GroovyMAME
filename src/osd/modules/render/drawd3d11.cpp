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
#include "rendersw.hxx"
#include "screen.h"

#include "modules/lib/osdlib.h"
//#include "modules/osdwindow.h"
#include "window.h"
#include "winmain.h"
#include "render_module.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <switchres/switchres.h>

#define MAX_BUFFER_WIDTH 3840
#define MAX_BUFFER_HEIGHT 2160

inline double get_ms(osd_ticks_t ticks) { return (double) ticks / osd_ticks_per_second() * 1000; };

/* renderer_d3d11 is the information about Direct3D 11 for the current screen */
class renderer_d3d11 : public osd_renderer
{
public:

	renderer_d3d11(osd_window &window, ID3D11Device *d3d11_device, IDXGIFactory2 *dxgi_factory, ID3D11DeviceContext *device_context);
	virtual ~renderer_d3d11() {};

	virtual int create() override;
	virtual render_primitive_list *get_primitives() override;
	virtual int draw(const int update) override;
/*
	virtual void save() override {};
	virtual void record() override {};
	virtual void toggle_fsfx() override {};
	virtual void add_audio_to_recording(const int16_t *buffer, int samples_this_frame) override {};
	virtual std::vector<ui::menu_item> get_slider_list() override { return {}; };
	virtual int restart() override { return 0; };
*/
private:
	Microsoft::WRL::ComPtr<IDXGISwapChain2> m_swapchain;
	ID3D11Device*             m_d3d11_device;             // Direct3D 11 device
	IDXGIFactory2*            m_dxgi_factory;             // Direct3D 11 device
	ID3D11DeviceContext*      m_device_context;
	ID3D11RenderTargetView*   m_backbuffer_rtv;
	ID3D11Texture2D*          m_cpu_tex;
	ID3D11ShaderResourceView* m_cpu_srv;
	ID3D11VertexShader*       m_vs;
	ID3D11PixelShader*        m_ps;
	ID3D11SamplerState*       m_sampler;

	int                     m_adapter;                  // ordinal adapter number
	int                     m_vendor_id;                // adapter vendor id
	int                     m_width;                    // current width
	int                     m_height;                   // current height
	int                     m_refresh;                  // current refresh rate
	int                     m_viewport_width;           // current viewport width
	int                     m_viewport_height;          // current viewport height
	int                     m_client_width;             // current window client width
	int                     m_client_height;            // current window client height
	bool                    m_interlace;                // current interlace
	int                     m_frame_delay;              // current frame delay value
	float m_pixel_aspect = 1.0;

	std::unique_ptr<uint8_t []> m_bmdata;
	size_t                      m_bmsize = 0;

	// Compile HLSL
	HRESULT CompileShader(LPCWSTR file, LPCSTR entry, LPCSTR target, ID3DBlob** blob)
	{
		ID3DBlob* error = nullptr;
		HRESULT hr = D3DCompileFromFile(file, nullptr, nullptr, entry, target,
										D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
										0, blob, &error);
		if (FAILED(hr))
		{
			if (error)
			{
				OutputDebugStringA((char*)error->GetBufferPointer());
				error->Release();
			}
		}
		return hr;
	}

	static inline BOOL GetClientRectExceptMenu(HWND hWnd, PRECT pRect, BOOL fullscreen)
	{
		static HMENU last_menu;
		static RECT last_rect;
		static RECT cached_rect;
		HMENU menu = GetMenu(hWnd);
		BOOL result = GetClientRect(hWnd, pRect);

		if (!fullscreen || !menu)
			return result;

		// to avoid flicker use cache if we can use
		if (last_menu != menu || memcmp(&last_rect, pRect, sizeof *pRect) != 0)
		{
			last_menu = menu;
			last_rect = *pRect;

			SetMenu(hWnd, nullptr);
			result = GetClientRect(hWnd, &cached_rect);
			SetMenu(hWnd, menu);
		}

		*pRect = cached_rect;
		return result;
	}

};

renderer_d3d11::renderer_d3d11(osd_window &window, ID3D11Device *d3d11_device, IDXGIFactory2 *dxgi_factory, ID3D11DeviceContext *device_context)
	: osd_renderer(window)
	, m_d3d11_device(d3d11_device)
	, m_dxgi_factory(dxgi_factory)
	, m_device_context(device_context)
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

	IDXGIDevice2 * dxgi_device;
	m_d3d11_device->QueryInterface(__uuidof(IDXGIDevice2), (void **)&dxgi_device);

	uint32_t max_frame_latency;
	dxgi_device->GetMaximumFrameLatency(&max_frame_latency);
	osd_printf_info("max_frame_latency %d\n", max_frame_latency);

	//dxgi_device->SetMaximumFrameLatency(1);

	dxgi_device->GetMaximumFrameLatency(&max_frame_latency);
	osd_printf_info("max_frame_latency %d\n", max_frame_latency);


	int sr_width = 0;
	int sr_height = 0;
	int sr_refresh = 0;
	//int sr_interlace = 0;

	switchres_manager *m_switchres = &downcast<windows_osd_interface&>(window().machine().osd()).switchres()->switchres();
	if (m_switchres->display(window().index()) != nullptr)
	{
		modeline *m_switchres_mode = m_switchres->display(window().index())->selected_mode();
		if (m_switchres_mode != nullptr)
		{
			sr_width = m_switchres_mode->type & MODE_ROTATED? m_switchres_mode->height : m_switchres_mode->width;
			sr_height = m_switchres_mode->type & MODE_ROTATED? m_switchres_mode->width : m_switchres_mode->height;
			sr_refresh = (int)m_switchres_mode->refresh;
	//		sr_interlace = m_switchres_mode->interlace;
		}
	}

	// Create swapchain
	DXGI_SWAP_CHAIN_DESC1 scd;
	memset(&scd, 0, sizeof(scd));
	scd.Width = sr_width;
	scd.Height = sr_height;
	scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	scd.Stereo = false;
	scd.SampleDesc.Count = 1;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.BufferCount = 2;
	scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	//scd.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
	scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsscd;
	memset(&fsscd, 0, sizeof(fsscd));
	fsscd.RefreshRate.Numerator = sr_refresh;
	fsscd.RefreshRate.Denominator = 1;
	//fsscd.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
	//fsscd.Scaling = DXGI_MODE_SCALING_STRETCHED;
	fsscd.Windowed = false;

	HRESULT hr;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
	hr = m_dxgi_factory->CreateSwapChainForHwnd(m_d3d11_device, hwnd, &scd, &fsscd, NULL, &swapchain1);
	osd_printf_error("CreateSwapChainForHwnd: %x\n", hr);
	hr = swapchain1.As(&m_swapchain);
	osd_printf_error("hr: %x\n", hr);
	m_swapchain->SetMaximumFrameLatency(1);


	RECT client;
	GetClientRectExceptMenu(hwnd, &client, window().fullscreen());
	m_client_width = client.right - client.left;
	m_client_height = client.bottom - client.top;

	window().target()->compute_visible_area(m_client_width, m_client_height, 1.0f, window().target()->orientation(), m_viewport_width, m_viewport_height);
	window().target()->compute_minimum_size(m_width, m_height);

	osd_printf_info("vp: %d %d, target: %d %d\n", m_viewport_width, m_viewport_height, m_width, m_height);

    // Backbuffer RTV
    ID3D11Texture2D* backbuffer = nullptr;
    hr = m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuffer);
    hr = m_d3d11_device->CreateRenderTargetView(backbuffer, nullptr, &m_backbuffer_rtv);
    backbuffer->Release();

    osd_printf_verbose("CreateRenderTargetView %d\n", hr);


    // Texture for CPU -> GPU
    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = m_width;
    tex_desc.Height = m_height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    //tex_desc.Usage = D3D11_USAGE_DYNAMIC;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = m_d3d11_device->CreateTexture2D(&tex_desc, nullptr, &m_cpu_tex);
    hr = m_d3d11_device->CreateShaderResourceView(m_cpu_tex, nullptr, &m_cpu_srv);

    // Sampler
    D3D11_SAMPLER_DESC samp = {};
    samp.Filter =  D3D11_FILTER_MIN_MAG_MIP_POINT; //D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    m_d3d11_device->CreateSamplerState(&samp, &m_sampler);

	// Shaders
	ID3DBlob* vs_blob = nullptr;
	ID3DBlob* ps_blob = nullptr;

	CompileShader(L"src\\osd\\modules\\render\\fullscreen.hlsl", "VSMain", "vs_5_0", &vs_blob);
	CompileShader(L"src\\osd\\modules\\render\\fullscreen.hlsl", "PSMain", "ps_5_0", &ps_blob);

	m_d3d11_device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &m_vs);
	m_d3d11_device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &m_ps);

	vs_blob->Release();
	ps_blob->Release();

    D3D11_VIEWPORT vp;
    vp.TopLeftX = (m_client_width - m_viewport_width) / 2;
    vp.TopLeftY = (m_client_height - m_viewport_height) / 2;
    vp.Width = (float) m_viewport_width;
    vp.Height = (float) m_viewport_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_device_context->RSSetViewports(1, &vp);

    float clear[4] = { 1, 0, 0, 1 };
    m_device_context->ClearRenderTargetView(m_backbuffer_rtv, clear);
    m_device_context->OMSetRenderTargets(1, &m_backbuffer_rtv, nullptr);

    m_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_device_context->IASetInputLayout(nullptr);

    m_device_context->VSSetShader(m_vs, nullptr, 0);
    m_device_context->PSSetShader(m_ps, nullptr, 0);
    m_device_context->PSSetShaderResources(0, 1, &m_cpu_srv);
    m_device_context->PSSetSamplers(0, 1, &m_sampler);

	return 0;
}

int renderer_d3d11::draw(const int update)
{
	#if defined(OSD_WINDOWS)
		auto &win = dynamic_cast<win_window_info &>(window());
	#elif defined(OSD_SDL)
		auto &win = dynamic_cast<sdl_window_info &>(window());
	#endif

	#if defined(OSD_WINDOWS)
	// we don't have any special resize behaviors
	if (win.m_resize_state == win_window_info::RESIZE_STATE_PENDING)
		win.m_resize_state = win_window_info::RESIZE_STATE_NORMAL;
	#endif

	// compute pitch of target
	int const pitch = (m_width + 3) & ~3;

	// make sure our temporary bitmap is big enough
	if ((pitch * m_height * 4) > m_bmsize)
	{
		m_bmsize = pitch * m_height * 4 * 2;
		m_bmdata.reset();
		m_bmdata = std::make_unique<uint8_t []>(m_bmsize);
		osd_printf_verbose("resize: %d %d %d %ld\n", m_width, m_height, pitch, m_bmsize);
	}


	int m_bilinear = 0;

	osd_ticks_t before_prim = osd_ticks();

	// draw the primitives to the bitmap
	win.m_primlist->acquire_lock();
	if (m_bilinear)
		software_renderer<uint32_t, 0,0,0, 16,8,0,0, 1>::draw_primitives(*win.m_primlist, m_bmdata.get(), m_width, m_height, pitch);
	else
		software_renderer<uint32_t, 0,0,0, 16,8,0,0, 0>::draw_primitives(*win.m_primlist, m_bmdata.get(), m_width, m_height, pitch);
	win.m_primlist->release_lock();

	osd_ticks_t after_prim = osd_ticks();
/*
    D3D11_MAPPED_SUBRESOURCE mapped;
    m_device_context->Map(m_cpu_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    for (int y = 0; y < m_height; y++)
    {
        memcpy((BYTE*)mapped.pData + y * mapped.RowPitch,
               m_bmdata.get() + y * pitch * 4,
               m_width * 4);
    }
    m_device_context->Unmap(m_cpu_tex, 0);
*/

	D3D11_BOX box;
	box.front = 0;
	box.back = 1;
	box.left = 0;
	box.right = m_width;
	box.top = 0;
	box.bottom = m_height;

	m_device_context->UpdateSubresource(m_cpu_tex, 0, &box, m_bmdata.get(), m_width * 4, m_width * m_height * 4);

	osd_ticks_t after_map = osd_ticks();

    float clear[4] = { 1, 0, 0, 1 };
    m_device_context->ClearRenderTargetView(m_backbuffer_rtv, clear);
    m_device_context->OMSetRenderTargets(1, &m_backbuffer_rtv, nullptr);

    m_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_device_context->IASetInputLayout(nullptr);

    m_device_context->VSSetShader(m_vs, nullptr, 0);
    m_device_context->PSSetShader(m_ps, nullptr, 0);
    m_device_context->PSSetShaderResources(0, 1, &m_cpu_srv);
    m_device_context->PSSetSamplers(0, 1, &m_sampler);

    m_device_context->Draw(3, 0); // fullscreen triangle

osd_ticks_t before_present = osd_ticks();

    m_swapchain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);

	osd_ticks_t after_present = osd_ticks();

	osd_printf_verbose("software_renderer: %.3f, memcpy: %.3f, present: %.3f total: %.3f\n",
		get_ms(after_prim - before_prim), get_ms(after_map - after_prim), get_ms(after_present - before_present), get_ms(after_present - before_prim));

    return 0;
}

//============================================================
//  renderer_d3d11::get_primitives
//============================================================

render_primitive_list *renderer_d3d11::get_primitives()
{
/*
	if (m_width == 0 || m_height == 0)
	{
		osd_dim const dimensions = window().get_size();
		if ((dimensions.width() <= 0) || (dimensions.height() <= 0))
			return nullptr;

		m_width = std::min(dimensions.width(), MAX_BUFFER_WIDTH);
		m_height = std::min(dimensions.height(), MAX_BUFFER_HEIGHT);
	}
*/
	m_pixel_aspect = (4.0/3.0) / ((float)m_width / m_height);

	osd_printf_verbose("get_primitives %d %d %f\n", m_width, m_height, m_pixel_aspect);

	window().target()->set_bounds(m_width, m_height, m_pixel_aspect);
	return &window().target()->get_primitives();
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
	using dxgi_create_dxgi_factory_fn = HRESULT (WINAPI *)(UINT Flags, REFIID riid, void **factory);
	dynamic_module::ptr m_d3d11_dll;
	dynamic_module::ptr m_dxgi_dll;
	Microsoft::WRL::ComPtr<ID3D11Device> m_d3d11_device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_device_context;
	Microsoft::WRL::ComPtr<IDXGIFactory2> m_dxgi_factory;
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

	(*d3d11_create_device)(
		NULL,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		D3D11_CREATE_DEVICE_SINGLETHREADED, //D3D11_CREATE_DEVICE_DEBUG,
		NULL,
		0,
		D3D11_SDK_VERSION,
		&m_d3d11_device,
		NULL,
		&m_device_context);

	if (!m_d3d11_device)
	{
		osd_printf_warning("Direct3D: Unable to initialize Direct3D 11\n");
		m_d3d11_dll.reset();
		m_options = nullptr;
		return -1;
	}

	m_dxgi_dll = dynamic_module::open({ "dxgi.dll" });
	auto const dxgi_create_dxgi_factory = m_dxgi_dll->bind<dxgi_create_dxgi_factory_fn>("CreateDXGIFactory2");
	if (!dxgi_create_dxgi_factory)
	{
		osd_printf_warning("Direct3D: Could not find CreateDXGIFactory1 function in dxgi.dll\n");
		m_dxgi_dll.reset();
		m_options = nullptr;
		return -1;
	}

	HRESULT hr;
	hr = (*dxgi_create_dxgi_factory)(0, __uuidof(IDXGIFactory2), &m_dxgi_factory);
	if (!m_dxgi_factory) osd_printf_verbose("dxgi hr: %x\n", hr);

	osd_printf_verbose("Direct3D: Using Direct3D 11\n");

	return 0;
}


//============================================================
//  video_d3d11::exit
//============================================================

void video_d3d11::exit()
{
	m_d3d11_device.Reset();
	m_device_context.Reset();
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
	return std::make_unique<renderer_d3d11>(window, m_d3d11_device.Get(), m_dxgi_factory.Get(), m_device_context.Get());
}

} // anonymous namespace

} // namespace osd

MODULE_DEFINITION(RENDERER_D3D11, osd::video_d3d11)
