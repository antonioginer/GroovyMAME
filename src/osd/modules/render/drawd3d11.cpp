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
#include "rendlay.h"
#include "render.h"
#include "rendutil.h"
#include "rendersw.hxx"
#include "screen.h"

#include "modules/lib/osdlib.h"
#include "modules/monitor/monitor_module.h"
#include "window.h"
#include "winmain.h"
#include "render_module.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <switchres/switchres.h>
#include "emusync.h"

#define LOG_SCANLINES 0
#define LOG_PRESENT_COUNT 0
#define DEVICE_FLAGS 0 //| D3D11_CREATE_DEVICE_DEBUG

#if LOG_PRESENT_COUNT
	#define emusync_printf_verbose(...) osd_printf_verbose(__VA_ARGS__)
#else
	#define emusync_printf_verbose(...)
#endif


//============================================================
//  log_debug_info
//============================================================

static void log_debug_info(ID3D11Device *device)
{
	ID3D11InfoQueue* infoQueue = nullptr;

	device->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&infoQueue);

	UINT64 count = infoQueue->GetNumStoredMessages();
	for (UINT64 i = 0; i < count; ++i)
	{
		SIZE_T size = 0;
		infoQueue->GetMessage(i, nullptr, &size);

		D3D11_MESSAGE* msg = (D3D11_MESSAGE*)malloc(size);
		infoQueue->GetMessage(i, msg, &size);

		osd_printf_error("D3D11: %s\n", msg->pDescription);
		free(msg);
	}
}


/* renderer_d3d11 is the information about Direct3D 11 for the current screen */
class renderer_d3d11 : public osd_renderer
{
public:

	renderer_d3d11(osd_window &window, ID3D11Device *d3d11_device, IDXGIFactory2 *dxgi_factory, ID3D11DeviceContext *device_context);
	virtual ~renderer_d3d11();

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
	bool create_resources();
	bool resize_buffers();
	void set_viewport();
	bool get_output();
	bool pick_best_mode(DXGI_MODE_DESC *mode);
	bool get_updated_dimensions();
	bool get_vblank_timestamp();
	uint64_t get_frame_counter();
	inline double get_ms(osd_ticks_t ticks) { return (double) ticks / osd_ticks_per_second() * 1000; };
	inline double time_now() { return get_ms(osd_ticks() - m_time_start); };

	ID3D11Device*             m_d3d11_device;             // Direct3D 11 device
	IDXGIFactory2*            m_dxgi_factory;             // Direct3D 11 device
	ID3D11DeviceContext*      m_device_context;
	IDXGIDevice2*             m_dxgi_device;
	IDXGISwapChain1*          m_swapchain;
	IDXGIAdapter*             m_adapter;
	IDXGIOutput*              m_output;
	ID3D11RenderTargetView*   m_backbuffer_rtv;
	ID3D11Texture2D*          m_cpu_tex;
	ID3D11ShaderResourceView* m_cpu_srv;
	ID3D11VertexShader*       m_vs;
	ID3D11PixelShader*        m_ps;
	ID3D11SamplerState*       m_sampler;
	ID3D11Buffer*             m_constant_buffer;
	D3D11_VIEWPORT            m_vp;

	int   m_width;                    // current width
	int   m_height;                   // current height
	int   m_refresh;                  // current refresh rate
	int   m_scale_mode;               // current target scale mode
	int   m_keep_aspect;              // current target keep aspect mode
	int   m_ismaximized;              // current window maximized state
	int   m_viewport_width;           // current viewport width
	int   m_viewport_height;          // current viewport height
	int   m_client_width;             // current window client width
	int   m_client_height;            // current window client height
	bool  m_interlace;                // current interlace
	bool  m_multimonitor;
	bool  m_fullscreen_failed;
	float m_pixel_aspect = 1.0;
	uint64_t m_time_start = 0;
	emusync &m_sync;

	// Options
	bool  m_filter = false;
	bool  m_autofilter = false;
	bool  m_switchres = false;

	struct factors
	{
		float x_factor;
		float y_factor;
		float padding[2];
	};
	factors m_factors = {};

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
	, m_adapter(nullptr)
	, m_output(nullptr)
	, m_backbuffer_rtv(nullptr)
	, m_cpu_tex(nullptr)
	, m_cpu_srv(nullptr)
	, m_vs(nullptr)
	, m_ps(nullptr)
	, m_sampler(nullptr)
	, m_constant_buffer(nullptr)
	, m_width(-1) // force get initial values
	, m_height(0)
	, m_refresh(0)
	, m_fullscreen_failed(false)
	, m_time_start(osd_ticks())
	, m_sync(window.sync())
{
}


//============================================================
//  renderer_d3d11::~renderer_d3d11
//============================================================

renderer_d3d11::~renderer_d3d11()
{
	if (m_constant_buffer != nullptr)
	{
		m_constant_buffer->Release();
		m_constant_buffer = nullptr;
	}
	if (m_sampler != nullptr)
	{
		m_sampler->Release();
		m_sampler = nullptr;
	}
	if (m_ps != nullptr)
	{
		m_ps->Release();
		m_ps = nullptr;
	}
	if (m_vs != nullptr)
	{
		m_vs->Release();
		m_vs = nullptr;
	}
	if (m_cpu_srv != nullptr)
	{
		m_cpu_srv->Release();
		m_cpu_srv = nullptr;
	}
	if (m_cpu_tex != nullptr)
	{
		m_cpu_tex->Release();
		m_cpu_tex = nullptr;
	}
	if (m_backbuffer_rtv != nullptr)
	{
		m_backbuffer_rtv->Release();
		m_backbuffer_rtv = nullptr;
	}
	if (m_swapchain != nullptr)
	{
		m_swapchain->SetFullscreenState(false, nullptr);
		m_swapchain->Release();
		m_swapchain = nullptr;
	}
	if (m_output != nullptr)
	{
		m_output->Release();
		m_output = nullptr;
	}
	if (m_adapter != nullptr)
	{
		m_adapter->Release();
		m_adapter = nullptr;
	}
	if (m_dxgi_device != nullptr)
	{
		m_dxgi_device->Release();
		m_dxgi_device = nullptr;
	}

	if (window().index() == 0) m_sync.osd_deinit();
}


//============================================================
//  renderer_d3d11::get_output()
//============================================================

bool renderer_d3d11::get_output()
{
	HRESULT hr;
	bool monitor_found = false;

	if (m_output != nullptr) m_output->Release();
	if (m_adapter != nullptr) m_adapter->Release();

	m_dxgi_device->GetAdapter(&m_adapter);
	if (!m_adapter)
		return false;

	for (UINT i = 0; m_adapter->EnumOutputs(i, &m_output) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		DXGI_OUTPUT_DESC desc;
		hr = m_output->GetDesc(&desc);
		if (SUCCEEDED(hr) && desc.Monitor == reinterpret_cast<HMONITOR>(window().monitor()->oshandle()))
		{
			monitor_found = true;
			break;
		}
	}

	if (!monitor_found)
		return false;

	return true;
}


//============================================================
//  renderer_d3d11::pick_best_mode
//============================================================

bool renderer_d3d11::pick_best_mode(DXGI_MODE_DESC *mode)
{

	HRESULT hr;
	DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
	UINT flags = DXGI_ENUM_MODES_INTERLACED;
	UINT num = 0;
	hr = m_output->GetDisplayModeList(format, flags, &num, 0);
	if (FAILED(hr)) return false;

	DXGI_MODE_DESC * ml = new DXGI_MODE_DESC[num];
	hr = m_output->GetDisplayModeList(format, flags, &num, ml);
	if (FAILED(hr)) return false;

	int sr_width = 0;
	int sr_height = 0;
	int sr_refresh = 0;
	int sr_interlace = 0;
	int sr_rotated = 0;

	switchres_manager *m_switchres = &downcast<windows_osd_interface&>(window().machine().osd()).switchres()->switchres();
	display_manager *display = m_switchres->display(window().index());
	if (display != nullptr)
	{
		modeline *m_switchres_mode = m_switchres->display(window().index())->selected_mode();
		if (m_switchres_mode != nullptr)
		{
			sr_rotated = m_switchres_mode->type & MODE_ROTATED;
			sr_width = sr_rotated? m_switchres_mode->height : m_switchres_mode->width;
			sr_height = sr_rotated? m_switchres_mode->width : m_switchres_mode->height;
			sr_refresh = (int)m_switchres_mode->refresh;
			sr_interlace = m_switchres_mode->interlace;
		}
	}

	bool match = false;
	for (UINT i = 0; i < num; i++)
	{
		DXGI_MODE_DESC *m = &ml[i];
		bool is_interlaced = m->ScanlineOrdering > 1;

		if (is_interlaced && m->RefreshRate.Denominator == 1)
			m->RefreshRate.Denominator = 2;

		if (m->Width == sr_width && m->Height == sr_height && int((float)m->RefreshRate.Numerator / (float)m->RefreshRate.Denominator) == sr_refresh && is_interlaced == sr_interlace)
		{
			osd_printf_verbose("->");
			match = true;
			*mode = *m;

			// Compute pixel aspect here
			float aspect = display->monitor_aspect();
			m_pixel_aspect = (sr_rotated? 1.0f / aspect : aspect) / ((float)sr_width / sr_height);
		}
		float refresh = m->RefreshRate.Denominator > 1? (float)m->RefreshRate.Numerator / (float)m->RefreshRate.Denominator : (float)m->RefreshRate.Numerator;
		osd_printf_verbose("mode %4d x%4d @ %.3f%s\n", m->Width, m->Height, refresh, is_interlaced? "i":"p");
	}

	delete [] ml;

	if (!match)
	{
		osd_printf_error("d3d11: could not find the requested video mode\n");
		return false;
	}

	return true;
}


//============================================================
//  renderer_d3d11::create
//============================================================

int renderer_d3d11::create()
{
	HRESULT hr;
	HWND hwnd = dynamic_cast<win_window_info &>(window()).platform_window();

	// Store required options
	windows_options &options = downcast<windows_options &>(window().machine().options());
	m_filter = options.filter();
	m_autofilter = options.autofilter();
	m_switchres = options.switch_res();
	m_multimonitor = options.numscreens() > 1;

	// Set max frame latency = 1
	m_d3d11_device->QueryInterface(__uuidof(IDXGIDevice2), (void **)&m_dxgi_device);
	m_dxgi_device->SetMaximumFrameLatency(1);

	// Determine swapchain's size and video mode if switching is allowed
	int sc_width = 0;
	int sc_height = 0;
	int sc_refresh = 0;
	int sc_interlace = 0;

	// Get required texture dimensions from game
	get_updated_dimensions();

	if (window().fullscreen() && m_switchres)
	{
		if (!get_output())
		{
			osd_printf_error("d3d11: could not find the specified output.\n");
			return -1;
		}

		DXGI_MODE_DESC mode {};
		if (pick_best_mode(&mode))
		{
			sc_width = mode.Width;
			sc_height = mode.Height;
			sc_refresh = mode.RefreshRate.Numerator;
			sc_interlace = mode.ScanlineOrdering > 1? 1 : 0;
		}
	}
	else
	{
		// Use window's client area in windowed mode
		RECT client;
		GetClientRectExceptMenu(hwnd, &client, window().fullscreen());
		m_client_width = client.right - client.left;
		m_client_height = client.bottom - client.top;
		sc_width = m_client_width;
		sc_height = m_client_height;
	}

	// Create swapchain
	DXGI_SWAP_CHAIN_DESC1 scd;
	memset(&scd, 0, sizeof(scd));
	scd.Width = sc_width;
	scd.Height = sc_height;
	scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	scd.Stereo = false;
	scd.SampleDesc.Count = 1;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.BufferCount = 1;
	scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsscd;
	memset(&fsscd, 0, sizeof(fsscd));
	fsscd.RefreshRate.Numerator = sc_refresh;
	fsscd.RefreshRate.Denominator = 1;
	fsscd.ScanlineOrdering = sc_interlace? DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST : DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
	fsscd.Windowed = !window().fullscreen();

	hr = m_dxgi_factory->CreateSwapChainForHwnd(m_d3d11_device, hwnd, &scd, &fsscd, NULL, &m_swapchain);
	if (FAILED(hr))
	{
		osd_printf_error("d3d11: CreateSwapChainForHwnd failed: %x\n", hr);
		return -1;
	}

	osd_printf_verbose("d3d11: swapchain created at: %dx%d\n", scd.Width, scd.Height);

	// Allow MAME to process ALT+Enter (this requires calling the parent factory)
	IDXGIFactory2 *parent_factory;
	m_swapchain->GetParent(__uuidof(IDXGIFactory2), (void **) &parent_factory);
	parent_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES);
	parent_factory->Release();

	// Create render target, fullscreen texture and sampler
	if (!create_resources())
		return -1;

	// Shaders
	ID3DBlob* vs_blob = nullptr;
	ID3DBlob* ps_blob = nullptr;

	const char *path = downcast<windows_options &>(window().machine().options()).screen_post_fx_dir();
	wchar_t shader_file[1024];
	swprintf(shader_file, 1024, L"%s\\autofilter.fx", path);

	hr = CompileShader(shader_file, "VS_Main", "vs_5_0", &vs_blob);
	if (FAILED(hr))
	{
		osd_printf_error("d3d11: failed compiling vertex shader: %x\n", hr);
		return -1;
	}
	hr = m_d3d11_device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &m_vs);
	if (FAILED(hr))
	{
		osd_printf_error("d3d11: failed creating vertex shader: %x\n", hr);
		return -1;
	}

	hr = CompileShader(shader_file, "PS_Main", "ps_5_0", &ps_blob);
		if (FAILED(hr))
	{
		osd_printf_error("d3d11: failed compiling pixel shader: %x\n", hr);
		return -1;
	}
	hr = m_d3d11_device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &m_ps);
	if (FAILED(hr))
	{
		osd_printf_error("d3d11: failed creating pixel shader: %x\n", hr);
		return -1;
	}

	vs_blob->Release();
	ps_blob->Release();

	// Create constant buffer
	D3D11_BUFFER_DESC cbd = {};
	cbd.Usage = D3D11_USAGE_DEFAULT;
	cbd.ByteWidth = sizeof(factors);
	cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbd.CPUAccessFlags = 0;
	hr = m_d3d11_device->CreateBuffer(&cbd, nullptr, &m_constant_buffer);
	if (FAILED(hr))
		osd_printf_error("d3d11: failed creating constant_buffer: %x\n", hr);

	float clear[4] = { 0, 0, 0, 1 };
	m_device_context->ClearRenderTargetView(m_backbuffer_rtv, clear);
	m_device_context->OMSetRenderTargets(1, &m_backbuffer_rtv, nullptr);

	m_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_device_context->IASetInputLayout(nullptr);

	m_device_context->VSSetShader(m_vs, nullptr, 0);
	m_device_context->PSSetShader(m_ps, nullptr, 0);
	m_device_context->PSSetShaderResources(0, 1, &m_cpu_srv);
	m_device_context->PSSetConstantBuffers(0, 1, &m_constant_buffer);

	m_device_context->PSSetSamplers(0, 1, &m_sampler);

	set_viewport();

	osd_printf_verbose("d3d11: device created.\n");

	if (window().index() == 0 && m_sync.sync_refresh())
		m_sync.osd_init(window().monitor()->oshandle(), std::bind(&renderer_d3d11::get_vblank_timestamp, this), std::bind(&renderer_d3d11::get_frame_counter, this));

	return 0;
}


//============================================================
//  renderer_d3d11::create_resources
//============================================================

bool renderer_d3d11::create_resources()
{
	HRESULT hr;

	// Backbuffer RTV
	ID3D11Texture2D* backbuffer = nullptr;
	hr = m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuffer);
	if (FAILED(hr))
	{
		osd_printf_error("d3d11: error getting backbuffer: %x\n", hr);
		return false;
	}

	hr = m_d3d11_device->CreateRenderTargetView(backbuffer, nullptr, &m_backbuffer_rtv);
	backbuffer->Release();
	if (FAILED(hr))
	{
		osd_printf_error("d3d11: error creating render target view: %x\n", hr);
		return false;
	}

	// Texture for CPU -> GPU
	if (m_cpu_srv != nullptr) m_cpu_srv->Release();
	if (m_cpu_tex != nullptr) m_cpu_tex->Release();
	get_updated_dimensions();

	D3D11_TEXTURE2D_DESC tex_desc = {};
	tex_desc.Width = m_width;
	tex_desc.Height = m_height;
	tex_desc.MipLevels = 1;
	tex_desc.ArraySize = 1;
	tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	tex_desc.SampleDesc.Count = 1;
	tex_desc.Usage = D3D11_USAGE_DEFAULT;
	tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	tex_desc.CPUAccessFlags = 0; //D3D11_CPU_ACCESS_WRITE;

	hr = m_d3d11_device->CreateTexture2D(&tex_desc, nullptr, &m_cpu_tex);
	if (FAILED(hr))
	{
		osd_printf_error("d3d11: error creating texture2D: %x\n", hr);
		log_debug_info(m_d3d11_device);
		return false;
	}

	// Pass it to our shader
	hr = m_d3d11_device->CreateShaderResourceView(m_cpu_tex, nullptr, &m_cpu_srv);
	if (FAILED(hr))
	{
		osd_printf_error("d3d11: error creating shader resource view: %x\n", hr);
		return false;
	}

	osd_printf_verbose("d3d11: texture2D created: %dx%d\n", m_width, m_height);

	// Create sampler
	if (m_sampler != nullptr) m_sampler->Release();
	if (m_autofilter) m_filter = (window().target()->scale_mode() == SCALE_FRACTIONAL);
	D3D11_SAMPLER_DESC samp = {};
	samp.Filter =  m_filter? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samp.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	samp.MinLOD = 0;
	samp.MaxLOD = D3D11_FLOAT32_MAX;
	hr = m_d3d11_device->CreateSamplerState(&samp, &m_sampler);
	if (FAILED(hr))
	{
		osd_printf_error("d3d11: error creating sampler state: %x\n", hr);
		return false;
	}

	return true;
}


//============================================================
//  renderer_d3d11::resize_buffers
//============================================================

bool renderer_d3d11::resize_buffers()
{
	HRESULT hr;

	if (window().fullscreen() && m_switchres)
	{
		m_sync.reset();

		DXGI_MODE_DESC mode {};
		pick_best_mode(&mode);

		HRESULT hr;
		mode.Format = DXGI_FORMAT_UNKNOWN;
		mode.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		hr = m_swapchain->ResizeTarget(&mode);
		if (FAILED(hr))
		{
			osd_printf_error("d3d11: error resizing target.\n");
			return false;
		}
	}

	m_device_context->OMSetRenderTargets(0, 0, 0);
	m_backbuffer_rtv->Release();
	hr = m_swapchain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
	if (FAILED(hr))
	{
		osd_printf_error("d3d11: error resizing buffers.\n");
		return false;
	}

	// Re-create resources
	if (!create_resources())
		return false;

	m_device_context->PSSetShader(m_ps, nullptr, 0);
	m_device_context->PSSetShaderResources(0, 1, &m_cpu_srv);
	m_device_context->PSSetSamplers(0, 1, &m_sampler);
	m_device_context->OMSetRenderTargets(1, &m_backbuffer_rtv, nullptr);

	set_viewport();

	return true;
}


//============================================================
//  renderer_d3d11::set_viewport
//============================================================

void renderer_d3d11::set_viewport()
{
	HWND hwnd = dynamic_cast<win_window_info &>(window()).platform_window();

	RECT client;
	GetClientRectExceptMenu(hwnd, &client, window().fullscreen());
	m_client_width = client.right - client.left;
	m_client_height = client.bottom - client.top;

	window().target()->compute_visible_area(m_client_width, m_client_height, m_pixel_aspect, window().target()->orientation(), m_viewport_width, m_viewport_height);
	get_updated_dimensions();

	osd_printf_verbose("d3d11: viewport: %dx%d, client: %dx%d, source: %dx%d, pixel_aspect: %.3f\n",
					m_viewport_width, m_viewport_height, m_client_width, m_client_height, m_width, m_height, m_pixel_aspect);

	m_vp.TopLeftX = (float)(m_client_width - m_viewport_width) / 2;
	m_vp.TopLeftY = (float)(m_client_height - m_viewport_height) / 2;
	m_vp.Width = (float) m_viewport_width;
	m_vp.Height = (float) m_viewport_height;
	m_vp.MinDepth = 0.0f;
	m_vp.MaxDepth = 1.0f;
	m_device_context->RSSetViewports(1, &m_vp);

	m_factors.x_factor = m_viewport_width % m_width != 0? ((float)m_width / m_viewport_width) / 2.0f : 0.0f;
	m_factors.y_factor = m_viewport_height % m_height != 0? ((float)m_height / m_viewport_height) / 2.0f : 0.0f;
	m_device_context->UpdateSubresource(m_constant_buffer, 0, nullptr, &m_factors, 0, 0);
	m_device_context->PSSetConstantBuffers(0, 1, &m_constant_buffer);
}


//============================================================
//  renderer_d3d11::get_vblank_timestamp
//============================================================

bool renderer_d3d11::get_vblank_timestamp()
{
	HRESULT hr;
	DXGI_FRAME_STATISTICS st;

	hr = m_swapchain->GetFrameStatistics(&st);
	if (FAILED(hr))
		return false;

	emusync_printf_verbose("prev present count: #%d [%d]\n", st.PresentCount, st.SyncRefreshCount - m_sync.first_sync_count());
	m_sync.register_vblank_in_ticks(st.SyncRefreshCount, st.SyncQPCTime.QuadPart);

	return true;
}


//============================================================
//  renderer_d3d11::get_frame_counter
//============================================================

uint64_t renderer_d3d11::get_frame_counter()
{
	DXGI_FRAME_STATISTICS st;

	uint32_t frame_count;
	m_swapchain->GetLastPresentCount(&frame_count);
	emusync_printf_verbose("this present count: #%d\n", frame_count);

	if (frame_count == 1)
	{
		osd_ticks_t time1 = osd_ticks(), time2;
		do
		{
			Sleep(1);
			time2 = osd_ticks();

			m_swapchain->GetFrameStatistics(&st);
		}
		while (st.PresentCount != 1 && get_ms(time2 - time1) < 300.0);
		osd_printf_verbose("d3d11: synchronizing with first timestamp: %.3f ms elapsed, stats: %d, %d, %d, %lld\n",
			get_ms(time2 - time1), st.PresentCount, st.PresentRefreshCount, st.SyncRefreshCount, st.SyncQPCTime.QuadPart);

		m_sync.register_vblank_in_ticks(st.SyncRefreshCount, st.SyncQPCTime.QuadPart);
	}

	return (uint64_t)frame_count;
}


//============================================================
//  renderer_d3d11::draw
//============================================================

int renderer_d3d11::draw(const int update)
{
	HRESULT hr;
	auto &win = dynamic_cast<win_window_info &>(window());

	// Check that both swapchain's & window's fullscreen states match.
	// This is required if fullscreen "optimizations" are enabled, since
	// the fullscreen state isn't properly restored back after alt-tabbing.
	if (window().fullscreen() && window().index() == 0)
	{
		IDXGIOutput *output = nullptr;
		int is_fullscreen = 0;
		if (FAILED(m_swapchain->GetFullscreenState(&is_fullscreen, &output)))
			osd_printf_error("GetFullscreenState failed\n");

		else if (!is_fullscreen)
		{
			hr = m_swapchain->SetFullscreenState(true, output);
			if (FAILED(hr))
			{
				osd_printf_error("d3d11: swapchain failed restoring fullscreen state for window(%d): %x\n", window().index(), hr);
				m_swapchain->SetFullscreenState(FALSE, nullptr);
				m_fullscreen_failed = true;
			}
			else
				osd_printf_info("SetFullscreenState(%d)\n", window().index());
		}
	}

	// if we're in the middle of resizing, leave things alone
	if (win.m_resize_state == win_window_info::RESIZE_STATE_RESIZING)
		return 0;

	// check if there's a client are resize pending
	if (win.m_resize_state == win_window_info::RESIZE_STATE_PENDING)
	{
		// Update size after user's border drag resize in windowed mode
		if (!resize_buffers())
			return -1;

		win.m_resize_state = win_window_info::RESIZE_STATE_NORMAL;
	}
	else
	{
		// This checks both an internal resolution change or an UI's scaling/aspect action
		if (get_updated_dimensions())
			if (!resize_buffers())
				return -1;
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

	//osd_ticks_t before_prim = osd_ticks();

	// draw the primitives to the bitmap
	win.m_primlist->acquire_lock();
	software_renderer<uint32_t, 0,0,0, 16,8,0,0, 0>::draw_primitives(*win.m_primlist, m_bmdata.get(), m_width, m_height, pitch);
	win.m_primlist->release_lock();

	//osd_ticks_t after_prim = osd_ticks();

	D3D11_BOX box;
	box.front = 0;
	box.back = 1;
	box.left = 0;
	box.right = m_width;
	box.top = 0;
	box.bottom = m_height;

	m_device_context->UpdateSubresource(m_cpu_tex, 0, &box, m_bmdata.get(), pitch * 4, pitch * m_height * 4);

	if (m_multimonitor)
	{
		m_device_context->PSSetShaderResources(0, 1, &m_cpu_srv);
		m_device_context->PSSetSamplers(0, 1, &m_sampler);
		m_device_context->PSSetConstantBuffers(0, 1, &m_constant_buffer);
		m_device_context->RSSetViewports(1, &m_vp);
		m_device_context->OMSetRenderTargets(1, &m_backbuffer_rtv, nullptr);
	}

	m_device_context->Draw(3, 0); // fullscreen triangle

	if (window().index() == 0) m_sync.predraw_sync();

#if LOG_SCANLINES
		uint32_t scanline;
		bool in_vblank = false;
		m_sync.get_scanline(&scanline, &in_vblank);
		osd_printf_verbose("scanline: %d in_vblank: %d vsync_offset %d\n", scanline, in_vblank, m_sync.vsync_offset());
#endif

	uint32_t interval = !m_sync.handle_throttle() && window().machine().video().throttled() && video_config.waitvsync ? 1 : 0;

	hr = m_swapchain->Present(interval, m_sync.sync_refresh() ? 0 : DXGI_PRESENT_DO_NOT_WAIT);
	if (FAILED(hr) && (hr != DXGI_ERROR_WAS_STILL_DRAWING))
		osd_printf_error("d3d11: swapchain Present failed: %x\n", hr);

	if (window().index() == 0) m_sync.postdraw_sync();

	return 0;
}


//============================================================
//  renderer_d3d11::get_primitives
//============================================================

render_primitive_list *renderer_d3d11::get_primitives()
{
	// Aspect ratio is handlend on the viewport, so here we just force the pixel aspect
	// so that our bounds (viewport) match the aspect ratio of the view

	float aspect = window().target()->current_view().effective_aspect() /
					(window().target()->orientation() & ORIENTATION_SWAP_XY? (float)m_height / m_width : (float)m_width / m_height);

	window().target()->set_bounds(m_width, m_height, aspect);;
	return &window().target()->get_primitives();
}


//============================================================
//  renderer_d3d11::get_updated_dimensions
//============================================================

bool renderer_d3d11::get_updated_dimensions()
{
	int32_t new_width, new_height, new_scale_mode, new_keepaspect, new_ismaximized;

	window().target()->compute_minimum_size(new_width, new_height);
	new_width *= window().prescale();
	new_height *= window().prescale();

	new_scale_mode = window().target()->scale_mode();
	new_keepaspect = window().target()->keepaspect();
	new_ismaximized = dynamic_cast<win_window_info &>(window()).m_ismaximized;

	if (new_width != m_width || new_height != m_height || new_scale_mode != m_scale_mode || new_keepaspect != m_keep_aspect || new_ismaximized != m_ismaximized)
	{
		m_width = new_width;
		m_height = new_height;
		m_scale_mode = new_scale_mode;
		m_keep_aspect = new_keepaspect;
		m_ismaximized = new_ismaximized;
		return true;
	}
	return false;
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
	std::vector<Microsoft::WRL::ComPtr<ID3D11Device>> m_d3d11_devices;
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

	m_dxgi_dll = dynamic_module::open({ "dxgi.dll" });
	auto const dxgi_create_dxgi_factory = m_dxgi_dll->bind<dxgi_create_dxgi_factory_fn>("CreateDXGIFactory2");
	if (!dxgi_create_dxgi_factory)
	{
		osd_printf_warning("Direct3D: Could not find CreateDXGIFactory2 function in dxgi.dll\n");
		m_dxgi_dll.reset();
		m_options = nullptr;
		return -1;
	}

	HRESULT hr;
	hr = (*dxgi_create_dxgi_factory)(0, __uuidof(IDXGIFactory2), &m_dxgi_factory);
	if (!m_dxgi_factory)
	{
		osd_printf_warning("Direct3D: failed creating DXGI factory: %x\n", hr);
		return -1;
	}

	osd_printf_verbose("Direct3D: Using Direct3D 11\n");

	return 0;
}


//============================================================
//  video_d3d11::exit
//============================================================

void video_d3d11::exit()
{
	for (Microsoft::WRL::ComPtr<ID3D11Device>& device : m_d3d11_devices) device.Reset();
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
	IDXGIAdapter* target_adapter;
	IDXGIOutput*  output;
	LUID target_luid = {};
	bool monitor_found = false;
	HRESULT hr;

	// Get output
	for (UINT i = 0; m_dxgi_factory->EnumAdapters(i, &target_adapter) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		for (UINT j = 0; target_adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; ++j)
		{
			DXGI_OUTPUT_DESC desc;
			output->GetDesc(&desc);
			if (desc.Monitor == reinterpret_cast<HMONITOR>(window.monitor()->oshandle()))
			{
				DXGI_ADAPTER_DESC desc;
				target_adapter->GetDesc(&desc);
				target_luid = desc.AdapterLuid;
				monitor_found = true;
				break;
			}
		}
		if (monitor_found) break;
	}

	if (!monitor_found)
	{
		osd_printf_warning("Direct3D: monitor not found!\n");
		goto error;
	}

	for (const Microsoft::WRL::ComPtr<ID3D11Device>& device : m_d3d11_devices)
	{
		if (!device) continue;

		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		if (FAILED(device.As(&dxgiDevice))) continue;

		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		if (FAILED(dxgiDevice->GetAdapter(&adapter))) continue;

		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);
		if (desc.AdapterLuid.LowPart == target_luid.LowPart && desc.AdapterLuid.HighPart == target_luid.HighPart)
		{
			m_d3d11_device = device;
			m_d3d11_device->GetImmediateContext(&m_device_context);
			break;
		}
	}

	// Device not found, create it
	if (!m_d3d11_device)
	{
		const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_1,  D3D_FEATURE_LEVEL_11_0 };

		auto const d3d11_create_device = m_d3d11_dll->bind<PFN_D3D11_CREATE_DEVICE>("D3D11CreateDevice");
		hr = (*d3d11_create_device)
			(target_adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, DEVICE_FLAGS, featureLevelArray, 2, D3D11_SDK_VERSION, &m_d3d11_device, NULL, &m_device_context);

		if (!m_d3d11_device) goto error;

		m_d3d11_devices.push_back(m_d3d11_device);
	}

	// Success
	return std::make_unique<renderer_d3d11>(window, m_d3d11_device.Get(), m_dxgi_factory.Get(), m_device_context.Get());

error:
	osd_printf_warning("Direct3D: Unable to initialize Direct3D 11: %x\n", hr);
	m_d3d11_dll.reset();
	std::exit(1);
}

} // anonymous namespace

} // namespace osd

MODULE_DEFINITION(RENDERER_D3D11, osd::video_d3d11)
