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
#include "render_module.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#define MAX_BUFFER_WIDTH 3840
#define MAX_BUFFER_HEIGHT 2160

/* renderer_d3d11 is the information about Direct3D 11 for the current screen */
class renderer_d3d11 : public osd_renderer
{
public:

	renderer_d3d11(osd_window &window, ID3D11Device *d3d11_device, IDXGIFactory1 *dxgi_factory, ID3D11DeviceContext *device_context);
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
	ID3D11Device*             m_d3d11_device;             // Direct3D 11 device
	IDXGIFactory1*            m_dxgi_factory;             // Direct3D 11 device
	ID3D11DeviceContext*      m_device_context;
	IDXGISwapChain*	          m_swapchain;
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

};

renderer_d3d11::renderer_d3d11(osd_window &window, ID3D11Device *d3d11_device, IDXGIFactory1 *dxgi_factory, ID3D11DeviceContext *device_context)
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

	m_width = 640;
	m_height = 480;

	// Create swapchain
	DXGI_SWAP_CHAIN_DESC scd;

	memset(&scd, 0, sizeof(scd));
	scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.SampleDesc.Count = 1;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.BufferCount = 1;
	scd.OutputWindow = hwnd;
	scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	scd.Windowed = TRUE;
	scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	m_dxgi_factory->CreateSwapChain(m_d3d11_device, &scd, &m_swapchain);

	HRESULT hr;

    // Backbuffer RTV
    ID3D11Texture2D* backbuffer = nullptr;
    hr = m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuffer);
    osd_printf_verbose("GetBuffer %d\n", hr);
    hr = m_d3d11_device->CreateRenderTargetView(backbuffer, nullptr, &m_backbuffer_rtv);
    osd_printf_verbose("CreateRenderTargetView %d\n", hr);
    backbuffer->Release();

    // Texture for CPU -> GPU
    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = 640; //m_width;
    tex_desc.Height = 480; //m_height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DYNAMIC;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    

    hr = m_d3d11_device->CreateTexture2D(&tex_desc, nullptr, &m_cpu_tex);
    osd_printf_verbose("CreateTexture2D %d\n", hr);
    hr = m_d3d11_device->CreateShaderResourceView(m_cpu_tex, nullptr, &m_cpu_srv);
    osd_printf_verbose("CreateShaderResourceView %d\n", hr);

    // Sampler
    D3D11_SAMPLER_DESC samp = {};
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
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

	// draw the primitives to the bitmap
	win.m_primlist->acquire_lock();
	if (m_bilinear)
		software_renderer<uint32_t, 0,0,0, 16,8,0,0, 1>::draw_primitives(*win.m_primlist, m_bmdata.get(), m_width, m_height, pitch);
	else
		software_renderer<uint32_t, 0,0,0, 16,8,0,0, 0>::draw_primitives(*win.m_primlist, m_bmdata.get(), m_width, m_height, pitch);
	win.m_primlist->release_lock();

	for (int y = 0; y < 480; y++)
	{
		for (int x = 0; x < 640 * 4; x++)
		{
			uint8_t *pix = m_bmdata.get();
			*pix = 0xff;
		}
	}

    D3D11_MAPPED_SUBRESOURCE mapped;
    m_device_context->Map(m_cpu_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    for (int y = 0; y < 480; y++)
    {
        memcpy((BYTE*)mapped.pData + y * mapped.RowPitch,
               m_bmdata.get() + y * pitch * 4,
               640 * 4);
    }
    m_device_context->Unmap(m_cpu_tex, 0);

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
    m_swapchain->Present(1, 0);

    return 0;
}

//============================================================
//  renderer_d3d11::get_primitives
//============================================================

render_primitive_list *renderer_d3d11::get_primitives()
{
	if (m_width == 0 || m_height == 0)
	{
		osd_dim const dimensions = window().get_size();
		if ((dimensions.width() <= 0) || (dimensions.height() <= 0))
			return nullptr;

		m_width = std::min(dimensions.width(), MAX_BUFFER_WIDTH);
		m_height = std::min(dimensions.height(), MAX_BUFFER_HEIGHT);
	}

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
	using dxgi_create_dxgi_factory_fn = HRESULT *(WINAPI *)(REFIID riid, void **factory);
	dynamic_module::ptr m_d3d11_dll;
	dynamic_module::ptr m_dxgi_dll;
	Microsoft::WRL::ComPtr<ID3D11Device> m_d3d11_device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_device_context;
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
		&m_device_context);

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
