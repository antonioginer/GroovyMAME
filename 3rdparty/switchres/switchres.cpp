/**************************************************************

   switchres.cpp - SwichRes core routines

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include <stdio.h>
#include <string.h>
#include "switchres.h"
#include "log.h"


//============================================================
//  logging
//============================================================

void switchres_manager::set_log_verbose_fn(void *func_ptr) { set_log_verbose((void *)func_ptr); }
void switchres_manager::set_log_info_fn(void *func_ptr) { set_log_info((void *)func_ptr); }
void switchres_manager::set_log_error_fn(void *func_ptr) { set_log_error((void *)func_ptr); }

//============================================================
//  switchres_manager::switchres_manager
//============================================================

switchres_manager::switchres_manager()
{
	// Set Switchres default config options
	set_monitor("generic_15");
	set_orientation("horizontal");
	set_modeline("auto");
	set_lcd_range("auto");
	for (int i = 0; i++ < MAX_RANGES;) set_crt_range(i, "auto");
	set_monitor_rotates_cw(false);

	// Set display manager default options
	set_screen("auto");
	set_modeline_generation(true);
	set_lock_unsupported_modes(true);
	set_lock_system_modes(true);
	set_refresh_dont_care(false);

	// Set modeline generator default options
	set_interlace(true);
	set_doublescan(true);
	set_dotclock_min(0.0f);
	set_rotation(false);
	set_monitor_aspect(STANDARD_CRT_ASPECT);
	set_refresh_tolerance(2.0f);
	set_super_width(2560);
}

//============================================================
//  switchres_manager::init
//============================================================

void switchres_manager::init()
{
	log_verbose("Switchres: v%s, Monitor: %s, Orientation: %s, Modeline generation: %s\n",
		SWITCHRES_VERSION, cs.monitor, cs.orientation, ds.modeline_generation?"enabled":"disabled");

	// Create our display manager
	m_display_factory = new display_manager();
	ds.gs = gs;
	m_display = m_display_factory->make(&ds);

	// Get user defined modeline
	modeline user_mode = {};
	if (ds.modeline_generation)
	{
		if (modeline_parse(cs.modeline, &user_mode))
		{
			user_mode.type |= MODE_USER_DEF;
			m_display->set_user_mode(&user_mode);
		}
	}

	// Get monitor specs
	if (user_mode.hactive)
	{
		modeline_to_monitor_range(m_display->range, &user_mode);
		monitor_show_range(m_display->range);
	}
	else
	{
		char default_monitor[] = "generic_15";
	
		memset(&m_display->range[0], 0, sizeof(struct monitor_range) * MAX_RANGES);

		if (!strcmp(cs.monitor, "custom"))
			for (int i = 0; i++ < MAX_RANGES;) monitor_fill_range(&m_display->range[i], cs.crt_range[i]);

		else if (!strcmp(cs.monitor, "lcd"))
			monitor_fill_lcd_range(&m_display->range[0], cs.lcd_range);

		else if (monitor_set_preset(cs.monitor, m_display->range) == 0)
			monitor_set_preset(default_monitor, m_display->range);
	}
}
