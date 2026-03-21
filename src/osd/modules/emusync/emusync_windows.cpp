// license:BSD-3-Clause
// copyright-holders:Antonio Giner, intealls
//============================================================
//
//  emusync_windows.cpp - Windows raster synchronization
//
//============================================================

#include <windows.h>
#include <ntdef.h>
#include <ntstatus.h>
#include <thread>
#include "emu.h"
#include "emusync.h"
#include "winmain.h"

// Windows SDK type definitions

typedef UINT D3DDDI_VIDEO_PRESENT_SOURCE_ID;
typedef UINT D3DKMT_HANDLE;

typedef struct _D3DKMT_OPENADAPTERFROMHDC
{
	HDC                            hDc;
	D3DKMT_HANDLE                  hAdapter;
	LUID                           AdapterLuid;
	D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;

typedef struct _D3DKMT_GETSCANLINE
{
	D3DKMT_HANDLE                  hAdapter;
	D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
	BOOLEAN                        InVerticalBlank;
	UINT                           ScanLine;
} D3DKMT_GETSCANLINE;

typedef struct _D3DKMT_WAITFORVERTICALBLANKEVENT
{
	D3DKMT_HANDLE                  hAdapter;
	D3DKMT_HANDLE                  hDevice;
	D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} D3DKMT_WAITFORVERTICALBLANKEVENT;

typedef NTSTATUS (*D3DKMT_GET_SCANLINE) (D3DKMT_GETSCANLINE *Arg1);
typedef NTSTATUS (*D3DKMT_OPEN_ADAPTER_FROM_HDC) (D3DKMT_OPENADAPTERFROMHDC *Arg1);
typedef NTSTATUS (*D3DKMT_WAIT_FOR_VERTICAL_BLANK_EVENT) (D3DKMT_WAITFORVERTICALBLANKEVENT *Arg1);

// Function pointers to gdi32 api
D3DKMT_GET_SCANLINE GetScanline;
D3DKMT_OPEN_ADAPTER_FROM_HDC OpenAdapterFromHdc;
D3DKMT_WAIT_FOR_VERTICAL_BLANK_EVENT WaitForVerticalBlankEvent;

// static variables
static D3DKMT_OPENADAPTERFROMHDC adapter_data = {};
static std::thread scan_poll;
static uint64_t vblank_timestamp = 0;
static uint64_t vblank_counter = 0;
static bool is_active = false;
static bool is_initialized = false;

static bool scanline_init(uint64_t monitor_handle, bool polling_thread);
static bool get_video_data(uint32_t *vactive, uint32_t *vtotal);


//============================================================
//  emusync:init_osd
//============================================================

bool emusync::osd_init(uint64_t monitor_handle, std::function<bool(void)> get_vblank_timestamp_external, std::function<uint64_t(void)> get_frame_counter_external)
{
	// If the renderer doesn't have a timestamp method, use Windows to get timestamps
	const windows_options& options = dynamic_cast<windows_options const &>(machine().options());
	bool use_polling_thread = (get_vblank_timestamp_external == nullptr || options.vblank_thread());

	get_vblank_timestamp = use_polling_thread ?
						std::bind(&emusync::get_vblank_timestamp_default, this) :
						get_vblank_timestamp_external;

	get_frame_counter = get_frame_counter_external;

	bool valid_adapter = scanline_init(monitor_handle, use_polling_thread);
	if (valid_adapter)
	{
		// Get vactive/vtotal from Windows API
		get_video_data(&m_vactive_osd, &m_vtotal_osd);
		compute_vactive_ratio();
	}

	return valid_adapter;
}


//============================================================
//  emusync::deinit
//============================================================

void emusync::osd_deinit()
{
	if (is_active)
	{
		is_active = false;
		scan_poll.join();
	}
}


//============================================================
//  emusync::get_vblank_timestamp_default
//============================================================

bool emusync::get_vblank_timestamp_default()
{
	if (is_initialized)
		register_vblank_in_ns(vblank_counter, vblank_timestamp);

	return is_initialized;
}


//============================================================
//  scanline_init
//============================================================

bool scanline_init(uint64_t monitor_handle, bool polling_thread)
{
	// Get api function hooks
	HINSTANCE hDLL;
	hDLL = LoadLibraryA("gdi32.dll");
	if (hDLL == NULL)
		return false;

	OpenAdapterFromHdc = (D3DKMT_OPEN_ADAPTER_FROM_HDC)GetProcAddress(hDLL,"D3DKMTOpenAdapterFromHdc");
	if (OpenAdapterFromHdc == NULL) return false;

	GetScanline = (D3DKMT_GET_SCANLINE)GetProcAddress(hDLL,"D3DKMTGetScanLine");
	if (GetScanline == NULL) return false;

	WaitForVerticalBlankEvent = (D3DKMT_WAIT_FOR_VERTICAL_BLANK_EVENT)GetProcAddress(hDLL,"D3DKMTWaitForVerticalBlankEvent");
	if (WaitForVerticalBlankEvent == NULL) return false;

	MONITORINFOEX mi;
	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfo((HMONITOR)monitor_handle, &mi))
		return false;

	// Get adapter from device name
	HDC hdc;
	hdc = CreateDC(NULL, mi.szDevice, NULL, NULL);
	if (hdc == NULL)
		return false;

	adapter_data.hDc = hdc;

	if ((*OpenAdapterFromHdc)(&adapter_data) != STATUS_SUCCESS)
	{
		DeleteDC(hdc);
		return false;
	}
	DeleteDC(hdc);

	if (!polling_thread)
		return true;

	// Create polling thread
	scan_poll = std::thread([]()
	{
		osd_printf_verbose("emusync: polling thread started.\n");
		is_active = true;

		const uint64_t ticks_to_ns = 1e9 / osd_ticks_per_second();

		while (is_active)
		{
			D3DKMT_WAITFORVERTICALBLANKEVENT vblank_data;
			vblank_data.hAdapter = adapter_data.hAdapter;
			vblank_data.hDevice = 0;
			vblank_data.VidPnSourceId = adapter_data.VidPnSourceId;

			if ((*WaitForVerticalBlankEvent)(&vblank_data) == STATUS_SUCCESS)
			{
				vblank_timestamp = osd_ticks() * ticks_to_ns;
				vblank_counter ++;
				is_initialized = true;
			}
		}

		osd_printf_verbose("emusync: polling thread destroyed\n");
	});

	return true;
}


//============================================================
//  emusync::get_scanline
//============================================================

void emusync::get_scanline(uint32_t *scanline, bool *in_vblank)
{
	// Poll new values
	D3DKMT_GETSCANLINE scanline_data;
	scanline_data.hAdapter = adapter_data.hAdapter;
	scanline_data.VidPnSourceId = adapter_data.VidPnSourceId;

	if ((*GetScanline)(&scanline_data) == STATUS_SUCCESS)
	{
		if (scanline != nullptr) *scanline = scanline_data.ScanLine;
		if (in_vblank != nullptr) *in_vblank = scanline_data.InVerticalBlank;
	}
}

//============================================================
//  get_vtotal
//============================================================

bool get_video_data(uint32_t *vactive, uint32_t *vtotal)
{
	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
	UINT32 flags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE;
	LONG result = ERROR_SUCCESS;

	do
	{
		// Determine how many path and mode structures to allocate
		UINT32 pathCount, modeCount;
		result = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);

		if (result != ERROR_SUCCESS)
			return false;

		// Allocate the path and mode arrays
		paths.resize(pathCount);
		modes.resize(modeCount);

		result = QueryDisplayConfig(flags, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);

		// The function may have returned fewer paths/modes than estimated
		paths.resize(pathCount);
		modes.resize(modeCount);
	} while (result == ERROR_INSUFFICIENT_BUFFER);

	// Find mode for out target adapter
	const LUID target = adapter_data.AdapterLuid;
	int target_idx = 0;
	int target_id = 0;

	for (const auto& path: paths)
	{
		if (path.targetInfo.adapterId.HighPart == target.HighPart && path.targetInfo.adapterId.LowPart == target.LowPart)
		{
			if (target_idx == adapter_data.VidPnSourceId)
			{
				target_id = path.targetInfo.id;
				break;
			}
			target_idx++;
		}
	}

	for (const auto& mode : modes)
	{
		if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET && mode.id == target_id &&
			(mode.adapterId.HighPart == target.HighPart && mode.adapterId.LowPart == target.LowPart))
		{
			const auto& signalInfo = mode.targetMode.targetVideoSignalInfo;

			uint64_t pixel_clock = signalInfo.pixelRate;
			auto active_size = signalInfo.activeSize;
			auto total_size = signalInfo.totalSize;

			if (active_size.cy != 0 && total_size.cy != 0)
			{
				osd_printf_verbose("emusync->get_video_data: pixel_clock: %lld htotal: %d vtotal: %d\n", pixel_clock, total_size.cx, total_size.cy);
				*vactive = active_size.cy;
				*vtotal = total_size.cy;
				return true;
			}

			if (signalInfo.hSyncFreq.Denominator != 0)
			{
				double hfreq = (double)signalInfo.hSyncFreq.Numerator / (double)signalInfo.hSyncFreq.Denominator;
				if (signalInfo.vSyncFreq.Denominator != 0)
				{
					double vfreq = (double)signalInfo.vSyncFreq.Numerator / (double)signalInfo.vSyncFreq.Denominator;
					*vtotal = round(hfreq / vfreq);
					osd_printf_verbose("emusync->get_video_data: hfreq: %.3f vfreq: %.3f vtotal: %d\n", hfreq, vfreq, *vtotal);
					return true;
				}
			}
		}
	}
	return false;
}
