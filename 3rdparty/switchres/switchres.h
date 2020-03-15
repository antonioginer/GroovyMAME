/**************************************************************

   switchres.h - SwichRes general header

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#ifndef __SWITCHRES_H__
#define __SWITCHRES_H__

#include "monitor.h"
#include "modeline.h"
#include "display.h"

//============================================================
//  CONSTANTS
//============================================================

#define SWITCHRES_VERSION "1.00"

//============================================================
//  TYPE DEFINITIONS
//============================================================

typedef struct config_settings
{
	bool mode_switching;
} config_settings;


class switchres_manager
{
public:

	switchres_manager();
	~switchres_manager()
	{
		if (m_display_factory) delete m_display_factory;
	};

	// getters
	display_manager *display() const { return m_display; }

	// setters (log manager)
	void set_log_verbose_fn(void *func_ptr);
	void set_log_info_fn(void *func_ptr);
	void set_log_error_fn(void *func_ptr);

	// setters (switchres manager)
	void set_monitor(const char *preset) { strncpy(ds.monitor, preset, sizeof(ds.monitor)-1); }
	void set_orientation(const char *orientation) { strncpy(ds.orientation, orientation, sizeof(ds.orientation)-1); }
	void set_modeline(const char *modeline) { strncpy(ds.modeline, modeline, sizeof(ds.modeline)-1); }
	void set_crt_range(int i, const char *range) { strncpy(ds.crt_range[i], range, sizeof(ds.crt_range[i])-1); }
	void set_lcd_range(const char *range) { strncpy(ds.lcd_range, range, sizeof(ds.lcd_range)-1); }
	void set_monitor_rotates_cw(bool value) { ds.monitor_rotates_cw = value; }

	// setters (display manager)
	void set_screen(const char *screen) { strncpy(ds.screen, screen, sizeof(ds.screen)-1); }
	void set_api(const char *api) { strncpy(ds.api, api, sizeof(ds.api)-1); }
	void set_modeline_generation(bool value) { ds.modeline_generation = value; }
	void set_lock_unsupported_modes(bool value) { ds.lock_unsupported_modes = value; }
	void set_lock_system_modes(bool value) { ds.lock_system_modes = value; }
	void set_refresh_dont_care(bool value) { ds.refresh_dont_care = value; }
	void set_ps_timing(const char *ps_timing) { strncpy(ds.ps_timing, ps_timing, sizeof(ds.ps_timing)-1); }

	//setters (modeline generator)
	void set_interlace(bool value) { gs.interlace = value; }
	void set_doublescan(bool value) { gs.doublescan = value; }
	void set_dotclock_min(double value) { gs.pclock_min = value * 1000000; }
	void set_refresh_tolerance(double value) { gs.refresh_tolerance = value; }
	void set_super_width(int value) { gs.super_width = value; }
	void set_rotation(bool value) { gs.rotation = value; }
	void set_monitor_aspect(double value) { gs.monitor_aspect = value; }

	// interface
	void init();

	//settings
	config_settings cs;
	display_settings ds;
	generator_settings gs;

private:

	display_manager *m_display_factory = 0;
	display_manager *m_display = 0;
};


#endif