/**************************************************************

   display.cpp - Display manager

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#include <stdio.h>
#include "display.h"
#if defined(_WIN32)
#include "display_windows.h"
#elif defined(__linux__)
#include <string.h>
#include "display_linux.h"
#endif
#include "log.h"

//============================================================
//  display_manager::make
//============================================================

display_manager *display_manager::make(display_settings *ds)
{
	display_manager *display = nullptr;

#if defined(_WIN32)
	display = new windows_display(ds);
#elif defined(__linux__)
	display = new linux_display(ds);
#endif

	return display;
}

//============================================================
//  display_manager::parse_options
//============================================================

void display_manager::parse_options()
{
	// Get user defined modeline
	modeline user_mode = {};
	if (m_ds.modeline_generation)
	{
		if (modeline_parse(m_ds.modeline, &user_mode))
		{
			user_mode.type |= MODE_USER_DEF;
			set_user_mode(&user_mode);
		}
	}

	// Get monitor specs
	if (user_mode.hactive)
	{
		modeline_to_monitor_range(range, &user_mode);
		monitor_show_range(range);
	}
	else
	{
		char default_monitor[] = "generic_15";

		memset(&range[0], 0, sizeof(struct monitor_range) * MAX_RANGES);

		if (!strcmp(m_ds.monitor, "custom"))
			for (int i = 0; i++ < MAX_RANGES;) monitor_fill_range(&range[i], m_ds.crt_range[i]);

		else if (!strcmp(m_ds.monitor, "lcd"))
			monitor_fill_lcd_range(&range[0], m_ds.lcd_range);

		else if (monitor_set_preset(m_ds.monitor, range) == 0)
			monitor_set_preset(default_monitor, range);
	}
}

//============================================================
//  display_manager::init
//============================================================

bool display_manager::init()
{
	sprintf(m_ds.screen, "ram");

	return true;
}

//============================================================
//  display_manager::caps
//============================================================

int display_manager::caps()
{
	if (video())
		return video()->caps();
	else
		return CUSTOM_VIDEO_CAPS_ADD;
}

//============================================================
//  display_manager::add_mode
//============================================================

bool display_manager::add_mode(modeline *mode)
{
	if (video() == nullptr)
		return false;

	// Add new mode
	if (!video()->add_mode(mode))
	{
		log_verbose("Switchres: error adding mode ");
		log_mode(mode);
		return false;		
	}

	log_verbose("Switchres: added ");
	log_mode(mode);

	return true;
}

//============================================================
//  display_manager::delete_mode
//============================================================

bool display_manager::delete_mode(modeline *mode)
{
	if (video() == nullptr)
		return false;

	if (!video()->delete_mode(mode))
	{
		log_verbose("Switchres: error deleting mode ");
		log_mode(mode);
		return false;
	}

	log_verbose("Switchres: deleted ");
	log_mode(mode);
	return true;
}

//============================================================
//  display_manager::update_mode
//============================================================

bool display_manager::update_mode(modeline *mode)
{
	if (video() == nullptr)
		return false;

	// Apply new timings
	if (!video()->update_mode(mode))
	{
		log_verbose("Switchres: error updating mode ");
		log_mode(mode);
		return false;
	}

	log_verbose("Switchres: updated ");
	log_mode(mode);
	return true;
}

//============================================================
//  display_manager::set_mode
//============================================================

bool display_manager::set_mode(modeline *)
{
	return false;
}

//============================================================
//  display_manager::log_mode
//============================================================

void display_manager::log_mode(modeline *mode)
{
	char modeline_txt[256];
	log_verbose("%s timing %s\n", video()->api_name(), modeline_print(mode, modeline_txt, MS_FULL));
}

//============================================================
//  display_manager::restore_modes
//============================================================

bool display_manager::restore_modes()
{
	bool error = false;

	// First, delete all modes we've added
	while (video_modes.size() > backup_modes.size())
	{
		delete_mode(&video_modes.back());
		video_modes.pop_back();
	}

	// Now restore all modes which timings have been modified
	for (unsigned i = video_modes.size(); i-- > 0; )
	{
		// Reset work fields
		video_modes[i].type = backup_modes[i].type = 0;
		video_modes[i].range = backup_modes[i].range = 0;

		if (memcmp(&video_modes[i], &backup_modes[i], sizeof(modeline) - sizeof(mode_result)) != 0)
		{
			video_modes[i] = backup_modes[i];
			if (!video()->update_mode(&video_modes[i]))
			{
				log_verbose("Switchres: error restoring mode ");
				log_mode(&video_modes[i]);
				error = true;
			}
			else
			{
				log_verbose("Switchres: restored ");
				log_mode(&video_modes[i]);
			}
		}
	}

	return !error;
}

//============================================================
//  display_manager::filter_modes
//============================================================

bool display_manager::filter_modes()
{
	for (auto &mode : video_modes)
	{
		// apply options to mode type
		if (m_ds.refresh_dont_care)
			mode.type |= V_FREQ_EDITABLE;

		if ((caps() & CUSTOM_VIDEO_CAPS_UPDATE))
			mode.type |= V_FREQ_EDITABLE;

		if (caps() & CUSTOM_VIDEO_CAPS_SCAN_EDITABLE)
			mode.type |= SCAN_EDITABLE;

		if (!m_ds.modeline_generation)
			mode.type &= ~(XYV_EDITABLE | SCAN_EDITABLE);

		if ((mode.type & MODE_DESKTOP) && !(caps() & CUSTOM_VIDEO_CAPS_DESKTOP_EDITABLE))
			mode.type &= ~V_FREQ_EDITABLE;

		if (m_ds.lock_system_modes && (mode.type & CUSTOM_VIDEO_TIMING_SYSTEM))
			mode.type |= MODE_DISABLED;

		// Lock all modes that don't match the user's -resolution rules
		if (m_user_mode.width != 0 || m_user_mode.height != 0 || m_user_mode.refresh == !0)
		{
			if (!( (mode.width == m_user_mode.width || (mode.type & X_RES_EDITABLE) || m_user_mode.width == 0)
				&& (mode.height == m_user_mode.height || (mode.type & Y_RES_EDITABLE) || m_user_mode.height == 0)
				&& (mode.refresh == m_user_mode.refresh || (mode.type & V_FREQ_EDITABLE) || m_user_mode.refresh == 0) ))
				mode.type |= MODE_DISABLED;
			else
				mode.type &= ~MODE_DISABLED;
		}

		// Make sure to unlock the desktop mode as fallback
		if (mode.type & MODE_DESKTOP)
			mode.type &= ~MODE_DISABLED;
	}

	return true;
}

//============================================================
//  display_manager::get_video_mode
//============================================================

modeline *display_manager::get_mode(int width, int height, float refresh, bool interlaced, bool rotated)
{
	modeline s_mode = {};
	modeline t_mode = {};
	modeline best_mode = {};
	char result[256]={'\x00'};

	log_verbose("Switchres: Calculating best video mode for %dx%d@%.6f%s orientation: %s\n",
						width, height, refresh, interlaced?"i":"", rotated?"rotated":"normal");

	best_mode.result.weight |= R_OUT_OF_RANGE;

	s_mode.interlace = interlaced;
	s_mode.vfreq = refresh;

	s_mode.hactive = normalize(width, 8);
	s_mode.vactive = height;
	m_ds.gs.rotation = rotated;
	if (rotated) std::swap(s_mode.hactive, s_mode.vactive);

	// Create a dummy mode entry if allowed
	if (caps() & CUSTOM_VIDEO_CAPS_ADD && m_ds.modeline_generation)
	{
		modeline new_mode = {};
		new_mode.type = XYV_EDITABLE | V_FREQ_EDITABLE | SCAN_EDITABLE | MODE_NEW;
		video_modes.push_back(new_mode);
	}

	// Run through our mode list and find the most suitable mode
	for (auto &mode : video_modes)
	{
		log_verbose("\nSwitchres: %s%4d%sx%s%4d%s_%s%d=%.6fHz%s%s\n",
			mode.type & X_RES_EDITABLE?"(":"[", mode.width, mode.type & X_RES_EDITABLE?")":"]",
			mode.type & Y_RES_EDITABLE?"(":"[", mode.height, mode.type & Y_RES_EDITABLE?")":"]",
			mode.type & V_FREQ_EDITABLE?"(":"[", mode.refresh, mode.vfreq, mode.type & V_FREQ_EDITABLE?")":"]",
			mode.type & MODE_DISABLED?" - locked":"");

		// now get the mode if allowed
		if (!(mode.type & MODE_DISABLED))
		{
			for (int i = 0 ; i < MAX_RANGES ; i++)
			{
				if (range[i].hfreq_min)
				{
					t_mode = mode;

					// init all editable fields with source or user values
					if (t_mode.type & X_RES_EDITABLE)
						t_mode.hactive = m_user_mode.width? m_user_mode.width : s_mode.hactive;

					if (t_mode.type & Y_RES_EDITABLE)
						t_mode.vactive = m_user_mode.height? m_user_mode.height : s_mode.vactive;

					if (mode.type & V_FREQ_EDITABLE)
						t_mode.vfreq = s_mode.vfreq;

					// lock resolution fields if required
					if (m_user_mode.width) t_mode.type &= ~X_RES_EDITABLE;
					if (m_user_mode.height) t_mode.type &= ~Y_RES_EDITABLE;

					modeline_create(&s_mode, &t_mode, &range[i], &m_ds.gs);
					t_mode.range = i;

					log_verbose("%s\n", modeline_result(&t_mode, result));

					if (modeline_compare(&t_mode, &best_mode))
					{
						best_mode = t_mode;
						m_best_mode = &mode;
					}
				}
			}
		}
	}

	// If we didn't need to create a new mode, remove our dummy entry
	if (caps() & CUSTOM_VIDEO_CAPS_ADD && m_ds.modeline_generation && !(best_mode.type & MODE_NEW))
		video_modes.pop_back();

	// If we didn't find a suitable mode, exit now
	if (best_mode.result.weight & R_OUT_OF_RANGE)
	{
		m_best_mode = 0;
		log_error("Switchres: could not find a video mode that meets your specs\n");
		return nullptr;
	}

	log_verbose("\nSwitchres: %s (%dx%d@%.6f)->(%dx%d@%.6f)\n", rotated?"vertical":"horizontal",
		width, height, refresh, best_mode.hactive, best_mode.vactive, best_mode.vfreq);

	log_verbose("%s\n", modeline_result(&best_mode, result));

	// Copy the new modeline to our mode list
	if (m_ds.modeline_generation && (best_mode.type & V_FREQ_EDITABLE))
	{
		if (best_mode.type & MODE_NEW)
		{
			best_mode.width = best_mode.hactive;
			best_mode.height = best_mode.vactive;
			best_mode.refresh = int(best_mode.vfreq);
			// lock new mode
			best_mode.type &= ~(X_RES_EDITABLE | Y_RES_EDITABLE | (caps() & CUSTOM_VIDEO_CAPS_UPDATE? 0 : V_FREQ_EDITABLE));
		}
		else
			best_mode.type |= MODE_UPDATED;

		char modeline[256]={'\x00'};
		log_info("Switchres: Modeline %s\n", modeline_print(&best_mode, modeline, MS_FULL));
	}

	*m_best_mode = best_mode;
	return m_best_mode;
}
