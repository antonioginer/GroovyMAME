/**************************************************************

   custom_video_xrandr.cpp - Linux XRANDR video management layer

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#include <stdio.h>
#include <dlfcn.h>
#include "custom_video_xrandr.h"
#include "log.h"

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

#define XCloseDisplay p_XCloseDisplay
#define XGrabServer p_XGrabServer
#define XOpenDisplay p_XOpenDisplay
#define XSync p_XSync
#define XUngrabServer p_XUngrabServer
#define XSetErrorHandler p_XSetErrorHandler

//============================================================
//  error_handler 
//  xorg error handler (static)
//============================================================

int xrandr_timing::m_xerrors = 0;
int xrandr_timing::m_xerrors_flag = 0;
int (*old_error_handler)(Display *, XErrorEvent *);

static __typeof__(XGetErrorText) *p_XGetErrorText;
#define XGetErrorText p_XGetErrorText

static int error_handler(Display *dpy, XErrorEvent *err)
{
	char buf[64];
	XGetErrorText(dpy, err->error_code, buf, 64);
	buf[0]='\0';
	xrandr_timing::m_xerrors|=xrandr_timing::m_xerrors_flag;
	log_error("XRANDR: <-,-> (error_handler) [ERROR] %s error code %d flags %02x\n", buf, err->error_code, xrandr_timing::m_xerrors);
	return 0;
}

//============================================================
//  xrandr_timing::xrandr_timing
//============================================================

xrandr_timing::xrandr_timing(char *device_name, char *param)
{
	log_verbose("XRANDR: <%p,%d> (xrandr_timing) creation (%s,%s)\n", this, m_desktop_output, device_name, param);
	// Copy screen device name and limit size
	if ((strlen(device_name)+1) > 32)
	{
		strncpy(m_device_name, device_name, 31);
		log_error("XRANDR: <%p,%d> (xrandr_timing) [ERROR] the devine name is too long it has been trucated to %s\n", this, m_desktop_output,m_device_name);
	} else {
		strcpy(m_device_name, device_name);
	}
}
//============================================================
//  xrandr_timing::~xrandr_timing
//============================================================

xrandr_timing::~xrandr_timing()
{
	// Free the display
	if (m_pdisplay != NULL)
		XCloseDisplay(m_pdisplay);

	// close Xrandr library
	if (m_xrandr_handle)
		dlclose(m_xrandr_handle);

	// close X11 library
	if (m_x11_handle)
		dlclose(m_x11_handle);

}

//============================================================
//  xrandr_timing::init
//============================================================

bool xrandr_timing::init()
{
	log_verbose("XRANDR: <%p,%d> (init) loading Xrandr library\n", this, m_desktop_output);
	if (!m_xrandr_handle)
		m_xrandr_handle = dlopen ("libXrandr.so", RTLD_NOW);
	if (m_xrandr_handle)
	{
		p_XRRAddOutputMode = (__typeof__(XRRAddOutputMode))dlsym(m_xrandr_handle,"XRRAddOutputMode");
		if (p_XRRAddOutputMode == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRAddOutputMode", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRConfigCurrentConfiguration = (__typeof__(XRRConfigCurrentConfiguration))dlsym(m_xrandr_handle,"XRRConfigCurrentConfiguration");
		if (p_XRRConfigCurrentConfiguration == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRConfigCurrentConfiguration", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRCreateMode = (__typeof__(XRRCreateMode))dlsym(m_xrandr_handle,"XRRCreateMode");
		if (p_XRRCreateMode == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRCreateMode", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRDeleteOutputMode = (__typeof__(XRRDeleteOutputMode))dlsym(m_xrandr_handle,"XRRDeleteOutputMode");
		if (p_XRRDeleteOutputMode == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRDeleteOutputMode", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRDestroyMode = (__typeof__(XRRDestroyMode))dlsym(m_xrandr_handle,"XRRDestroyMode");
		if (p_XRRDestroyMode == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRDestroyMode", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRFreeCrtcInfo = (__typeof__(XRRFreeCrtcInfo))dlsym(m_xrandr_handle,"XRRFreeCrtcInfo");
		if (p_XRRFreeCrtcInfo == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRFreeCrtcInfo", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRFreeOutputInfo = (__typeof__(XRRFreeOutputInfo))dlsym(m_xrandr_handle,"XRRFreeOutputInfo");
		if (p_XRRFreeOutputInfo == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRFreeOutputInfo", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRFreeScreenConfigInfo = (__typeof__(XRRFreeScreenConfigInfo))dlsym(m_xrandr_handle,"XRRFreeScreenConfigInfo");
		if (p_XRRFreeScreenConfigInfo == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRFreeScreenConfigInfo", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRFreeScreenResources = (__typeof__(XRRFreeScreenResources))dlsym(m_xrandr_handle,"XRRFreeScreenResources");
		if (p_XRRFreeScreenResources == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRFreeScreenResources", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRGetCrtcInfo = (__typeof__(XRRGetCrtcInfo))dlsym(m_xrandr_handle,"XRRGetCrtcInfo");
		if (p_XRRGetCrtcInfo == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRGetCrtcInfo", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRGetOutputInfo = (__typeof__(XRRGetOutputInfo))dlsym(m_xrandr_handle,"XRRGetOutputInfo");
		if (p_XRRGetOutputInfo == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRGetOutputInfo", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRGetScreenInfo = (__typeof__(XRRGetScreenInfo))dlsym(m_xrandr_handle,"XRRGetScreenInfo");
		if (p_XRRGetScreenInfo == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRGetScreenInfo", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRGetScreenResourcesCurrent = (__typeof__(XRRGetScreenResourcesCurrent))dlsym(m_xrandr_handle,"XRRGetScreenResourcesCurrent");
		if (p_XRRGetScreenResourcesCurrent == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRGetScreenResourcesCurrent", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRQueryVersion = (__typeof__(XRRQueryVersion))dlsym(m_xrandr_handle,"XRRQueryVersion");
		if (p_XRRQueryVersion == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRQueryVersion", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRSetCrtcConfig = (__typeof__(XRRSetCrtcConfig))dlsym(m_xrandr_handle,"XRRSetCrtcConfig");
		if (p_XRRSetCrtcConfig == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRSetCrtcConfig", "XRANDR_LIBRARY");
			return false;
		}

		p_XRRSetScreenSize = (__typeof__(XRRSetScreenSize))dlsym(m_xrandr_handle,"XRRSetScreenSize");
		if (p_XRRSetScreenSize == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s", this, m_desktop_output, "XRRSetScreenSize", "XRANDR_LIBRARY");
			return false;
		}
	} else {
		log_error("XRANDR: <%p,%d> (init) [ERROR] missing %s library\n", this, m_desktop_output, "XRANDR_LIBRARY");
		return false;
	}

	log_verbose("XRANDR: <%p,%d> (init) loading X11 library\n", this, m_desktop_output);
	if (!m_x11_handle)
		m_x11_handle = dlopen ("libX11.so", RTLD_NOW);
	if (m_x11_handle)
	{
		p_XCloseDisplay = (__typeof__(XCloseDisplay))dlsym(m_x11_handle,"XCloseDisplay");
		if (p_XCloseDisplay == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s\n", this, m_desktop_output, "XCloseDisplay", "X11_LIBRARY");
			return false;
		}

		p_XGrabServer = (__typeof__(XGrabServer)) dlsym(m_x11_handle,"XGrabServer");
		if (p_XGrabServer == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s\n", this, m_desktop_output, "XGrabServer", "X11_LIBRARY");
			return false;
		}

		p_XOpenDisplay = (__typeof__(XOpenDisplay))dlsym(m_x11_handle,"XOpenDisplay");
		if (p_XOpenDisplay == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s\n", this, m_desktop_output, "XOpenDisplay", "X11_LIBRARY");
			return false;
		}

		p_XSync = (__typeof__(XSync))dlsym(m_x11_handle,"XSync");
		if (p_XSync == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s\n", this, m_desktop_output, "XSync", "X11_LIBRARY");
			return false;
		}

		p_XUngrabServer = (__typeof__(XUngrabServer))dlsym(m_x11_handle,"XUngrabServer");
		if (p_XUngrabServer == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s\n", this, m_desktop_output, "XUngrabServer", "X11_LIBRARY");
			return false;
		}

		p_XSetErrorHandler = (__typeof__(XSetErrorHandler))dlsym(m_x11_handle,"XSetErrorHandler");
		if (p_XSetErrorHandler == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s\n", this, m_desktop_output, "XSetErrorHandler", "X11_LIBRARY");
			return false;
		}

		p_XGetErrorText = (__typeof__(XGetErrorText))dlsym(m_x11_handle,"XGetErrorText");
		if (p_XGetErrorText == NULL)
		{
			log_error("XRANDR: <%p,%d> (init) [ERROR] missing func %s in %s\n", this, m_desktop_output, "XGetErrorText", "X11_LIBRARY");
			return false;
		}
	} else {
		log_error("XRANDR: <%p,%d> (init) [ERROR] missing %s library\n", this, m_desktop_output, "X11_LIBRARY");
		return false;
	}


	// Select current display and root window
	// m_pdisplay is global to reduce open/close calls, resource is freed when class is destroyed
	if (!m_pdisplay)
		m_pdisplay = XOpenDisplay(NULL);

	if (!m_pdisplay)
	{
		log_verbose("XRANDR: <%p,%d> (init) [ERROR] failed to connect to the X server\n", this, m_desktop_output);
		return false;
	}

	// Display XRANDR version
	int major_version, minor_version;
	XRRQueryVersion(m_pdisplay, &major_version, &minor_version);
	log_verbose("XRANDR: <%p,%d> (init) version %d.%d\n", this, m_desktop_output,major_version,minor_version);

	// screen_pos defines screen position, 0 is default first screen position and equivalent to 'auto'
	int screen_pos = -1;
	bool detected = false;
	
	// Handle the screen name, "auto", "screen[0-9]" and XRANDR device name
	if (strlen(m_device_name) == 7 && !strncmp(m_device_name,"screen",6) && m_device_name[6]>='0' && m_device_name[6]<='9')
		screen_pos = m_device_name[6]-'0';

	for (int screen = 0;!detected && screen < ScreenCount(m_pdisplay);screen++)
	{
		log_verbose("XRANDR: <%p,%d> (init) check screen number %d\n", this, m_desktop_output, screen);
		m_root = RootWindow(m_pdisplay, screen);
		
		XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);

		// Get default screen rotation from screen configuration
		XRRScreenConfiguration *sc = XRRGetScreenInfo(m_pdisplay, m_root);
		XRRConfigCurrentConfiguration(sc, &m_desktop_rotation);
		XRRFreeScreenConfigInfo(sc);

		Rotation current_rotation = 0;
		int output_position = 0;
		for (int o = 0;o < resources->noutput;o++)
		{
			XRROutputInfo *output_info = XRRGetOutputInfo(m_pdisplay, resources, resources->outputs[o]);
			if (!output_info)
				log_error("XRANDR: <%p,%d> (init) [ERROR] could not get output 0x%x information\n", this, m_desktop_output, (uint) resources->outputs[o]);

			// Check all connected output
			log_verbose("XRANDR: <%p,%d> (init) check output connector '%s' active %d crtc %d\n", this, m_desktop_output, output_info->name, output_info->connection == RR_Connected?1:0, output_info->crtc?1:0);
			if (m_desktop_output == -1 && output_info->connection == RR_Connected && output_info->crtc)
			{
				if (!strcmp(m_device_name, "auto") || !strcmp(m_device_name,output_info->name) || output_position == screen_pos)
				{
					// store the output connector
					m_desktop_output = o;
					log_verbose("XRANDR: <%p,%d> (init) name '%s' id %d selected as primary output\n", this, m_desktop_output, output_info->name, o);

					XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_pdisplay, resources, output_info->crtc);
					current_rotation = crtc_info->rotation;
					// identify the current modeline id
					for (int m = 0;m < resources->nmode && m_desktop_mode.id == 0;m++)
					{
						// Get screen mode
						if (crtc_info->mode == resources->modes[m].id)
							m_desktop_mode = resources->modes[m];
					}
					XRRFreeCrtcInfo(crtc_info);

					// check screen rotation (left or right)
					if (current_rotation & 0xe)
					{
						m_crtc_flags = MODE_ROTATED;
						log_verbose("XRANDR: <%p,%d> (init) desktop rotation is %s\n", this, m_desktop_output, (current_rotation & 0x2)?"left":((current_rotation & 0x8)?"right":"inverted"));
					}
				}
				output_position++;
			}
			XRRFreeOutputInfo(output_info);
		}
		XRRFreeScreenResources(resources);

		// set if screen is detected
		detected = m_desktop_output != -1;
	}

	// Handle no screen detected case
	if(!detected)
		log_error("XRANDR: <%p,%d> (init) [ERROR] no screen detected\n", this, m_desktop_output);

	return detected;
}

//============================================================
//  xrandr_timing::update_mode
//============================================================

bool xrandr_timing::update_mode(modeline *mode)
{
	if (!mode)
		return false;

	// Handle no screen detected case
	if (m_desktop_output == -1)
	{
		log_error("XRANDR: <%p,%d> (update_mode) [ERROR] no screen detected\n", this, m_desktop_output);
		return false;
	}

	if (!delete_mode(mode))
	{
		log_error("XRANDR: <%p,%d> (update_mode) [ERROR] delete operation not successful", this, m_desktop_output);
		return false;
	}

	if (!add_mode(mode))
	{
		log_error("XRANDR: <%p,%d> (update_mode) [ERROR] add operation not successful", this, m_desktop_output);
		return false;
	}

	return true;
}
//============================================================
//  xrandr_timing::add_mode
//============================================================

bool xrandr_timing::add_mode(modeline *mode)
{
	if (!mode)
		return false;

	// Handle no screen detected case
	if (m_desktop_output == -1)
	{
		log_error("XRANDR: <%p,%d> (add_mode) [ERROR] no screen detected\n", this, m_desktop_output);
		return false;
	}

	if (find_mode(mode) != NULL)
	{
		log_error("XRANDR: <%p,%d> (add_mode) [ERROR] mode already exist\n", this, m_desktop_output);
	}

	// Create specific mode name
	char name[48];
	sprintf(name,"SR-%p,%d_%dx%d_%f", this, m_desktop_output, mode->hactive, mode->vactive, mode->vfreq);

	log_verbose("XRANDR: <%p,%d> (add_mode) create mode %s\n", this, m_desktop_output, name);

	// Setup the xrandr mode structure
	XRRModeInfo xmode = {};
	xmode.name       = name;
	xmode.nameLength = strlen(name);
	xmode.dotClock   = mode->pclock;
	xmode.width      = mode->hactive;
	xmode.hSyncStart = mode->hbegin;
	xmode.hSyncEnd   = mode->hend;
	xmode.hTotal     = mode->htotal;
	xmode.height     = mode->vactive;
	xmode.vSyncStart = mode->vbegin;
	xmode.vSyncEnd   = mode->vend;
	xmode.vTotal     = mode->vtotal;
	xmode.modeFlags  = (mode->interlace?RR_Interlace:0) | (mode->doublescan?RR_DoubleScan:0) | (mode->hsync?RR_HSyncPositive:RR_HSyncNegative) | (mode->vsync?RR_VSyncPositive:RR_VSyncNegative);
	xmode.hSkew      = 0;
		
	mode->type |= CUSTOM_VIDEO_TIMING_XRANDR;

	// Create the modeline
	XSync(m_pdisplay, False);
	m_xerrors = 0;
	m_xerrors_flag = 0x01;
	old_error_handler = XSetErrorHandler(error_handler);
	RRMode gmid = XRRCreateMode(m_pdisplay, m_root, &xmode);
	XSync(m_pdisplay, False);
	XSetErrorHandler(old_error_handler);
	if (m_xerrors & m_xerrors_flag)
	{
		log_error("XRANDR: <%p,%d> (add_mode) [ERROR] in %s\n", this, m_desktop_output,"XRRCreateMode");
		return false;
	} 
	else 
	{
		mode->platform_data = gmid;
	}

	// Add new modeline to primary output
	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);

	XSync(m_pdisplay, False);
	m_xerrors_flag = 0x02;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRAddOutputMode(m_pdisplay, resources->outputs[m_desktop_output], gmid);
	XSync(m_pdisplay, False);
	XSetErrorHandler(old_error_handler);

	XRRFreeScreenResources(resources);

	if (m_xerrors & m_xerrors_flag)
	{
		log_error("XRANDR: <%p,%d> (add_mode) [ERROR] in %s\n", this, m_desktop_output,"XRRAddOutputMode");

		// remove unlinked modeline
		if (gmid) 
		{
			log_error("XRANDR: <%p,%d> (add_mode) [ERROR] remove mode [%04lx]\n", this, m_desktop_output, gmid);
			XRRDestroyMode(m_pdisplay, gmid);
		}
	}

	return m_xerrors==0;
}

//============================================================
//  xrandr_timing::find_mode
//============================================================

XRRModeInfo *xrandr_timing::find_mode(modeline *mode)
{
	XRRModeInfo *pxmode=NULL;
	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);

	// use platform_data (mode id) to return the mode
	for (int m = 0;m < resources->nmode && !pxmode;m++)
	{
		if (mode->platform_data == resources->modes[m].id)
			pxmode = &resources->modes[m];
	}

	XRRFreeScreenResources(resources);

	return pxmode;
}

//============================================================
//  xrandr_timing::set_timing
//============================================================

bool xrandr_timing::set_timing(modeline *mode)
{
	// Handle no screen detected case
	if (m_desktop_output == -1)
	{
		log_error("XRANDR: <%p,%d> (set_timing) [ERROR] no screen detected\n", this, m_desktop_output);
		return false;
	}

	XRRModeInfo *pxmode = NULL;
	
	if (mode->type & MODE_DESKTOP)
	{
		pxmode = &m_desktop_mode;
	} else {
		pxmode = find_mode(mode);
	}

	if (pxmode == NULL)
	{
		log_error("XRANDR: <%p,%d> (set_timing) [ERROR] mode not found\n", this, m_desktop_output);
		return false;
	}

	// Use xrandr to switch to new mode.
	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);
	XRROutputInfo *output_info = XRRGetOutputInfo(m_pdisplay, resources, resources->outputs[m_desktop_output]);
	XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_pdisplay, resources, output_info->crtc);

	if (pxmode->id == crtc_info->mode)
	{
		log_verbose("XRANDR: <%p,%d> (set_timing) mode [%04lx] is already active\n", this, m_desktop_output,pxmode->id);
		XRRFreeCrtcInfo(crtc_info);
		XRRFreeOutputInfo(output_info);
		XRRFreeScreenResources(resources);
		return true;
	}

	m_xerrors = 0;

	log_verbose("XRANDR: <%p,%d> (set_timing) switching mode [%04lx] %ux%u+%d+%d --> [%04lx] %ux%u+%d+%d\n", this, m_desktop_output, crtc_info->mode, crtc_info->width, crtc_info->height, crtc_info->x, crtc_info->y, pxmode->id, pxmode->width, pxmode->height, crtc_info->x, crtc_info->y);

	// Grab X server to prevent unwanted interaction from the window manager
	XGrabServer(m_pdisplay);

	unsigned int width=0;
	unsigned int height=0;

	int x_offset = pxmode->width - crtc_info->width;
	int y_offset = pxmode->height - crtc_info->height;
	int bound_width = crtc_info->x + crtc_info->width;
	int bound_height = crtc_info->y + crtc_info->height;

	XRRCrtcInfo *global_crtc = new XRRCrtcInfo[resources->ncrtc];

	// caculate necessary screen size and replace the crtc neighbors if they have at least one side aligned with the mode changed crtc 
	for (int c = 0;c < resources->ncrtc;c++)
	{
		memcpy(&global_crtc[c], XRRGetCrtcInfo(m_pdisplay, resources, resources->crtcs[c]), sizeof(XRRCrtcInfo));
		XRRCrtcInfo *crtc_info2 = &global_crtc[c];
		//log_verbose("****************** XRANDR: <%p,%d> (set_timing) <debug> crtc time information %ld\n", this, m_desktop_output, crtc_info2->timestamp); // to be deleted in final version
		if ( resources->crtcs[c] == output_info->crtc)
		{
			//log_verbose("****************** XRANDR: <%p,%d> (set_timing) <debug> impacted crtc use new mode parameters\n", this, m_desktop_output); // to be deleted in final version

			// switchres output, use new mode info
			if (crtc_info->x + pxmode->width > width)
				width=crtc_info->x + pxmode->width;

			if (crtc_info->y + pxmode->height > height)
				height=crtc_info->y + pxmode->height;

			crtc_info2->mode = pxmode->id;
			crtc_info2->timestamp = 1;
		} else {
			//log_verbose("****************** XRANDR: <%p,%d> (set_timing) <debug> neighborhood original crtc %d: %04lx %dx%d+%d+%d\n", this, m_desktop_output, c, crtc_info2->mode, crtc_info2->width, crtc_info2->height, crtc_info2->x, crtc_info2->y); // to be deleted in final version

			// relocate crtc impacted by new width
			if ( crtc_info2->x >= bound_width )
			{
				crtc_info2->x += x_offset;
				crtc_info2->timestamp = 1;
			}

			// relocate crtc impacted by new  height
			if ( crtc_info2->y >= bound_height )
			{
				crtc_info2->y += y_offset;
				crtc_info2->timestamp = 1;
			}

			// calculate size based on crtc placement
			if (crtc_info2->x + crtc_info2->width > width)
				width=crtc_info2->x + crtc_info2->width;
			if (crtc_info2->y + crtc_info2->height > height)
				height=crtc_info2->y + crtc_info2->height;

			//log_verbose("****************** XRANDR: <%p,%d> (set_timing) <debug> \\___ neighborhood new crtc %d: %04lx %dx%d+%d+%d\n", this, m_desktop_output, c, crtc_info2->mode, crtc_info2->width, crtc_info2->height, crtc_info2->x, crtc_info2->y); // to be deleted in final version
		}
		//log_verbose("XRANDR: <%p,%d> (set_timing) <debug> screen size estimation %d x %d\n", this, m_desktop_output, width, height); // to be deleted in final version
	}

	// Disable all CRTC
	for (int c = 0;c < resources->ncrtc;c++)
	{
		XRRCrtcInfo *crtc_info2 = XRRGetCrtcInfo(m_pdisplay, resources, resources->crtcs[c]);
		//log_verbose("XRANDR: <%p,%d> (set_timing) <debug> disable crtc %d mode id %04lx time %ld/%ld\n", this, m_desktop_output, c, crtc_info2->mode, crtc_info2->timestamp, global_crtc[c].timestamp);
		// checking mode might not be necessary due to timestamp value 
		if ( crtc_info2->mode != 0 && global_crtc[c].timestamp == 1)
		{
			if (XRRSetCrtcConfig(m_pdisplay, resources, resources->crtcs[c], CurrentTime, 0, 0, None, RR_Rotate_0, NULL, 0) != RRSetConfigSuccess)
			{
				log_error("XRANDR: <%p,%d> (set_timing) [ERROR] when disabling CRTC\n", this, m_desktop_output);
				m_xerrors_flag = 0x01;
				m_xerrors |= m_xerrors_flag;
			}
		}
		XRRFreeCrtcInfo(crtc_info2);
	}

	// Set the framebuffer screen size to enable all CRTC
        if (m_xerrors == 0)
	{
		log_verbose("XRANDR: <%p,%d> (set_timing) changing screen size to %d x %d\n", this, m_desktop_output, width, height);
		XSync(m_pdisplay, False);
		m_xerrors_flag = 0x02;
		old_error_handler = XSetErrorHandler(error_handler);
		XRRSetScreenSize(m_pdisplay, m_root, width, height, (25.4 * width) / 96.0, (25.4 * height) / 96.0);
		XSync(m_pdisplay, False);
		XSetErrorHandler(old_error_handler);
		if (m_xerrors & m_xerrors_flag)
			log_error("XRANDR: <%p,%d> (set_timing) [ERROR] in %s\n", this, m_desktop_output,"XRRSetScreenSize");
	}

	// Refresh all CRTC, switch modeline and set new placement
	for (int c = 0;c < resources->ncrtc;c++)
	{
		XRRCrtcInfo *crtc_info2 = &global_crtc[c];
		// checking mode might not be necessary due to timestamp value
		if ( crtc_info2->mode != 0 && crtc_info2->timestamp == 1)
		{
			// enable CRTC with updated parameters
			XSync(m_pdisplay, False);
			m_xerrors_flag = 0x14;
			old_error_handler = XSetErrorHandler(error_handler);
			//log_verbose("XRANDR: <%p,%d> (set_timing) <debug> crtc %d set modeline %04lx\n", this, m_desktop_output, c, crtc_info2->mode);
			XRRSetCrtcConfig(m_pdisplay, resources, resources->crtcs[c], CurrentTime, crtc_info2->x, crtc_info2->y, crtc_info2->mode, crtc_info2->rotation, crtc_info2->outputs, crtc_info2->noutput);
			XSync(m_pdisplay, False);
			if (m_xerrors & 0x10)
			{
				log_error("XRANDR: <%p,%d> (set_timing) [ERROR] in %s\n", this, m_desktop_output,"XRRSetCrtcConfig");
				m_xerrors &= 0xEF;
			}
		}
	}
	delete[] global_crtc;

	// Release X server, events can be processed now
	XUngrabServer(m_pdisplay);

	XRRFreeCrtcInfo(crtc_info);

	if (m_xerrors & m_xerrors_flag)
		log_error("XRANDR: <%p,%d> (set_timing) [ERROR] in %s\n", this, m_desktop_output,"XRRSetCrtcConfig");

	// Recall the impacted crtc to settle parameters
	crtc_info = XRRGetCrtcInfo(m_pdisplay, resources, output_info->crtc);

	// log crtc config modeline change fail 
	if (crtc_info->mode == 0)
		log_error("XRANDR: <%p,%d> (set_timing) [ERROR] switching resolution, no modeline\n", this, m_desktop_output);

	// Verify current active mode
	for (int m = 0;m < resources->nmode && crtc_info->mode;m++)
	{
		XRRModeInfo *pxmode2 = &resources->modes[m];
		if (pxmode2->id == crtc_info->mode)
			log_verbose("XRANDR: <%p,%d> (set_timing) active mode [%04lx] name %s clock %6.6fMHz %ux%u+%d+%d\n", this, m_desktop_output, pxmode2->id, pxmode2->name, (double)pxmode2->dotClock / 1000000.0, pxmode->width, pxmode->height, crtc_info->x, crtc_info->y);
	}

	XRRFreeCrtcInfo(crtc_info);
	XRRFreeOutputInfo(output_info);
	XRRFreeScreenResources(resources);

	return m_xerrors==0;
}

//============================================================
//  xrandr_timing::delete_mode
//============================================================

bool xrandr_timing::delete_mode(modeline *mode)
{
	// Handle no screen detected case
	if (m_desktop_output == -1)
	{
		log_error("XRANDR: <%p,%d> (delete_mode) [ERROR] no screen detected\n", this, m_desktop_output);
		return false;
	}

	if (!mode)
		return false;

	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);

	int total_xerrors = 0;
	// Delete modeline
	for (int m = 0;m < resources->nmode && mode->platform_data != 0;m++)
	{
		if (mode->platform_data == resources->modes[m].id)
		{
			XRROutputInfo *output_info = XRRGetOutputInfo(m_pdisplay, resources, resources->outputs[m_desktop_output]);
			XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_pdisplay, resources, output_info->crtc);
			if (resources->modes[m].id == crtc_info->mode)
				log_error("XRANDR: <%p,%d> (delete_mode) [WARNING] modeline [%04lx] is currently active\n", this, m_desktop_output,resources->modes[m].id);

			XRRFreeCrtcInfo(crtc_info);
			XRRFreeOutputInfo(output_info);

			log_verbose("XRANDR: <%p,%d> (delete_mode) remove mode %s\n", this, m_desktop_output, resources->modes[m].name);

			XSync(m_pdisplay, False);
			m_xerrors = 0;
			m_xerrors_flag = 0x01;
			old_error_handler = XSetErrorHandler(error_handler);
			XRRDeleteOutputMode(m_pdisplay, resources->outputs[m_desktop_output], resources->modes[m].id);
			if (m_xerrors & m_xerrors_flag)
			{
				log_error("XRANDR: <%p,%d> (delete_mode) [ERROR] in %s\n", this, m_desktop_output,"XRRDeleteOutputMode");
				total_xerrors++;
			}

			m_xerrors_flag = 0x02;
			XRRDestroyMode(m_pdisplay, resources->modes[m].id);
			XSync(m_pdisplay, False);
			XSetErrorHandler(old_error_handler);
			if (m_xerrors & m_xerrors_flag)
			{
				log_error("XRANDR: <%p,%d> (delete_mode) [ERROR] in %s\n", this, m_desktop_output,"XRRDestroyMode");
				total_xerrors++;
			}
			mode->platform_data = 0;
		}
	}

	XRRFreeScreenResources(resources);

	return total_xerrors==0;
}

//============================================================
//  xrandr_timing::get_timing
//============================================================

bool xrandr_timing::get_timing(modeline *mode)
{
	// Handle no screen detected case
	if (m_desktop_output == -1)
	{
		log_error("XRANDR: <%p,%d> (get_timing) [ERROR] no screen detected\n", this, m_desktop_output);
		return false;
	}

	XRRScreenResources *resources = XRRGetScreenResourcesCurrent(m_pdisplay, m_root);
	XRROutputInfo *output_info = XRRGetOutputInfo(m_pdisplay, resources, resources->outputs[m_desktop_output]);

	// Cycle through the modelines and report them back to the display manager
	if (m_video_modes_position < output_info->nmode)
	{
		for (int m = 0;m < resources->nmode;m++)
		{
			XRRModeInfo *pxmode = &resources->modes[m];

			if (pxmode->id==output_info->modes[m_video_modes_position]) 
			{
				mode->platform_data = pxmode->id;

				mode->pclock  	= pxmode->dotClock;
				mode->hactive 	= pxmode->width;
				mode->hbegin  	= pxmode->hSyncStart;
				mode->hend    	= pxmode->hSyncEnd;
				mode->htotal  	= pxmode->hTotal;
				mode->vactive 	= pxmode->height;
				mode->vbegin  	= pxmode->vSyncStart;
				mode->vend    	= pxmode->vSyncEnd;
				mode->vtotal  	= pxmode->vTotal;
				mode->interlace = (pxmode->modeFlags & RR_Interlace)?1:0;
				mode->doublescan = (pxmode->modeFlags & RR_DoubleScan)?1:0;
				mode->hsync     = (pxmode->modeFlags & RR_HSyncPositive)?1:0;
				mode->vsync     = (pxmode->modeFlags & RR_VSyncPositive)?1:0;

				mode->hfreq 	= mode->pclock / mode->htotal;
				mode->vfreq 	= mode->hfreq / mode->vtotal * (mode->interlace?2:1);
				mode->refresh 	= mode->vfreq;

				mode->width	= pxmode->width;
				mode->height	= pxmode->height;
		
				// Add the rotation flag from the crtc
				mode->type |= m_crtc_flags;

				if (strncmp(pxmode->name,"SR-",3) == 0) {
					log_verbose("XRANDR: <%p,%d> (get_timing) [WARNING] modeline %s detected\n", this, m_desktop_output, pxmode->name);
					mode->type |= CUSTOM_VIDEO_TIMING_XRANDR;
				} else {
					mode->type |= CUSTOM_VIDEO_TIMING_SYSTEM;
				}
		
				// Add the desktop flag to desktop modeline
				if (m_desktop_mode.id == pxmode->id)
					mode->type |= MODE_DESKTOP;
			}
		} 
		m_video_modes_position++;
	} else {
		// Inititalise the position for the modeline list
		m_video_modes_position = 0;
	}

	XRRFreeOutputInfo(output_info);
	XRRFreeScreenResources(resources);

	return true;
}
