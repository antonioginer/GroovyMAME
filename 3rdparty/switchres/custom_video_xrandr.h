/**************************************************************

   custom_video_xrandr.h - Linux XRANDR video management layer

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

// X11 Xrandr headers
#include <X11/extensions/Xrandr.h>
#include <string.h>
#include "display.h"

class xrandr_timing : public custom_video
{
	public:
		xrandr_timing(char *device_name, char *param);
		~xrandr_timing();
		const char *api_name() { return "XRANDR"; }
		int caps() { return CUSTOM_VIDEO_CAPS_ADD; }
		bool init();

		bool add_mode(modeline *mode);
		bool delete_mode(modeline *mode);
		bool update_mode(modeline *mode);

		bool get_timing(modeline *mode);
		bool set_timing(modeline *mode);

		static int m_xerrors;
		static int m_xerrors_flag;

	private:
		void *m_xrandr_handle = 0;

		__typeof__(XRRAddOutputMode) *p_XRRAddOutputMode;
		__typeof__(XRRConfigCurrentConfiguration) *p_XRRConfigCurrentConfiguration;
		__typeof__(XRRCreateMode) *p_XRRCreateMode;
		__typeof__(XRRDeleteOutputMode) *p_XRRDeleteOutputMode;
		__typeof__(XRRDestroyMode) *p_XRRDestroyMode;
		__typeof__(XRRFreeCrtcInfo) *p_XRRFreeCrtcInfo;
		__typeof__(XRRFreeOutputInfo) *p_XRRFreeOutputInfo;
		__typeof__(XRRFreeScreenConfigInfo) *p_XRRFreeScreenConfigInfo;
		__typeof__(XRRFreeScreenResources) *p_XRRFreeScreenResources;
		__typeof__(XRRGetCrtcInfo) *p_XRRGetCrtcInfo;
		__typeof__(XRRGetOutputInfo) *p_XRRGetOutputInfo;
		__typeof__(XRRGetScreenInfo) *p_XRRGetScreenInfo;
		__typeof__(XRRGetScreenResourcesCurrent) *p_XRRGetScreenResourcesCurrent;
		__typeof__(XRRQueryVersion) *p_XRRQueryVersion;
		__typeof__(XRRSetCrtcConfig) *p_XRRSetCrtcConfig;
		__typeof__(XRRSetScreenSize) *p_XRRSetScreenSize;

#define XRRAddOutputMode p_XRRAddOutputMode
#define XRRConfigCurrentConfiguration p_XRRConfigCurrentConfiguration
#define XRRCreateMode p_XRRCreateMode
#define XRRDeleteOutputMode p_XRRDeleteOutputMode
#define XRRDestroyMode p_XRRDestroyMode
#define XRRFreeCrtcInfo p_XRRFreeCrtcInfo
#define XRRFreeOutputInfo p_XRRFreeOutputInfo
#define XRRFreeScreenConfigInfo p_XRRFreeScreenConfigInfo
#define XRRFreeScreenResources p_XRRFreeScreenResources
#define XRRGetCrtcInfo p_XRRGetCrtcInfo
#define XRRGetOutputInfo p_XRRGetOutputInfo
#define XRRGetScreenInfo p_XRRGetScreenInfo
#define XRRGetScreenResourcesCurrent p_XRRGetScreenResourcesCurrent
#define XRRQueryVersion p_XRRQueryVersion
#define XRRSetCrtcConfig p_XRRSetCrtcConfig
#define XRRSetScreenSize p_XRRSetScreenSize

		void *m_x11_handle = 0;

		__typeof__(XCloseDisplay) *p_XCloseDisplay;
		__typeof__(XGrabServer) *p_XGrabServer;
		__typeof__(XOpenDisplay) *p_XOpenDisplay;
		__typeof__(XSync) *p_XSync;
		__typeof__(XUngrabServer) *p_XUngrabServer;
		__typeof__(XSetErrorHandler) *p_XSetErrorHandler;

#define XCloseDisplay p_XCloseDisplay
#define XGrabServer p_XGrabServer
#define XOpenDisplay p_XOpenDisplay
#define XSync p_XSync
#define XUngrabServer p_XUngrabServer
#define XSetErrorHandler p_XSetErrorHandler

		XRRModeInfo *find_mode(modeline *mode);

		int m_video_modes_position = 0;
		char m_device_name[32];
		Rotation m_desktop_rotation;

		Display *m_pdisplay = NULL;
		Window m_root;

		int m_desktop_output = -1;
		XRRModeInfo m_desktop_mode = {};
		int m_crtc_flags = 0;
};
