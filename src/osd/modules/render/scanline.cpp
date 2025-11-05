// license:BSD-3-Clause
// copyright-holders:Antonio Giner
//============================================================
//
//  scanline.cpp - Windows scanline polling
//
//============================================================

#include <windows.h>
#include <ntdef.h>
#include <ntstatus.h>
#include <thread>
#include "scanline.h"
#include "emu.h"
#include "emusync.h"

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
static D3DKMT_OPENADAPTERFROMHDC adapter_data;
static std::thread scan_poll;
static uint64_t vblank_timestamp = 0;
static uint64_t vblank_counter = 0;
static bool is_active = false;
static bool is_initialized = false;

bool scanline_init(uint64_t monitor_handle, bool polling_thread);
void scanline_poll(uint32_t *scanline, bool *in_vblank);


//============================================================
//  emusync:init_osd
//============================================================

void emusync::osd_init(uint64_t monitor_handle, std::function<bool(void)> get_vblank_timestamp_external, std::function<uint64_t(void)> get_frame_counter_external)
{
	get_vblank_timestamp = get_vblank_timestamp_external == nullptr ?
						std::bind(&emusync::get_vblank_timestamp_default, this) :
						get_vblank_timestamp_external;

	get_frame_counter = get_frame_counter_external;

	// If the renderer doesn't have a timestamp method, use Windows to get timestamps
	bool use_polling_thread = (get_vblank_timestamp_external == nullptr);

	scanline_init(monitor_handle, use_polling_thread);
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

		while (is_active)
		{
			D3DKMT_WAITFORVERTICALBLANKEVENT vblank_data;
			vblank_data.hAdapter = adapter_data.hAdapter;
			vblank_data.hDevice = 0;
			vblank_data.VidPnSourceId = adapter_data.VidPnSourceId;

			if ((*WaitForVerticalBlankEvent)(&vblank_data) == STATUS_SUCCESS)
			{
				struct timespec monotime;
				clock_gettime(CLOCK_MONOTONIC, &monotime);
				vblank_timestamp = (uint64_t)(monotime.tv_sec) * (uint64_t)1000000000 + (uint64_t)(monotime.tv_nsec);
				vblank_counter ++;
				is_initialized = true;
			}
		}

		osd_printf_verbose("emusync: polling thread destroyed\n");
	});

	return true;
}


//============================================================
//  scanline_poll
//============================================================

void scanline_poll(uint32_t *scanline, bool *in_vblank)
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

