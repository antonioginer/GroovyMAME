// license:BSD-3-Clause
// copyright-holders:Antonio Giner
//============================================================
//
//  scanline.j - Windows scanline polling
//
//============================================================

#include <ntdef.h>
#include <ntstatus.h>
#include <thread>

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

// osd module data
D3DKMT_OPENADAPTERFROMHDC adapter_data;
D3DKMT_GETSCANLINE scanline_data;
D3DKMT_WAITFORVERTICALBLANKEVENT vblank_data;

// static variables
static std::thread scan_poll;
static uint64_t vblank_timestamp;
static uint64_t vblank_counter = 0;

//============================================================
//  scanline_init
//============================================================

bool scanline_init(const char *output_name)
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

	// Get adapter from device name
	HDC hdc;
	hdc = CreateDCA(NULL, output_name, NULL, NULL);
	if (hdc == NULL)
		return false;

	adapter_data.hDc = hdc;

	if ((*OpenAdapterFromHdc)(&adapter_data) != STATUS_SUCCESS)
	{
		DeleteDC(hdc);
		return false;
	}
	DeleteDC(hdc);

	// Create polling thread
	//scan_poll = std::thread(&vblank_poll);

	return true;
}

//============================================================
//  scanline_poll
//============================================================

void scanline_poll(uint32_t *scanline, bool *in_vblank)
{
	// Poll new values

	scanline_data.hAdapter = adapter_data.hAdapter;
	scanline_data.VidPnSourceId = adapter_data.VidPnSourceId;
	if ((*GetScanline)(&scanline_data) == STATUS_SUCCESS)
	{
		if (scanline != nullptr) *scanline = scanline_data.ScanLine;
		if (in_vblank != nullptr) *in_vblank = scanline_data.InVerticalBlank;
	}
}

//============================================================
//  scanline_poll
//============================================================

void wait_for_vertical_blank()
{
	vblank_data.hAdapter = adapter_data.hAdapter;
	vblank_data.VidPnSourceId = adapter_data.VidPnSourceId;
	if ((*WaitForVerticalBlankEvent)(&vblank_data) == STATUS_SUCCESS)
	{
		struct timespec monotime;
		clock_gettime(CLOCK_MONOTONIC, &monotime);
		vblank_timestamp = (uint64_t)(monotime.tv_sec) * (uint64_t)1000000000 + (uint64_t)(monotime.tv_nsec);
		vblank_counter ++;
	}
}
