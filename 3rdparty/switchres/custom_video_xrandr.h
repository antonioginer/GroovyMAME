/**************************************************************

   custom_video_xrandr.h - Linux XRANDR video management layer

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 - Chris Kennedy, Antonio Giner, Alexandre Wodarczyk

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
		XRRModeInfo *find_mode(modeline *mode);

		int m_video_modes_position = 0;
		char m_device_name[32];
		Rotation m_desktop_rotation;

		Display *m_pdisplay;
		Window m_root;

		int m_desktop_output = -1;
		XRRModeInfo m_desktop_mode = {};
		int m_crtc_flags = 0;

};
