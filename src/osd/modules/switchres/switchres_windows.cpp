/**************************************************************

   switchres_windows.cpp - Windows OSD SwitchRes core routines

   -----------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   GroovyMAME  Integration of SwitchRes into the MAME project
               Some reworked patches from SailorSat's CabMAME

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

// standard windows headers
#include <windows.h>

// MAME headers
#include "emu.h"
#include "emuopts.h"
#include "../../frontend/mame/mameopts.h"

// MAMEOS headers
#include "winmain.h"
#include "window.h"

// Custom video headers	
#include "custom_video.h"

#ifdef _MSC_VER
#define min(a, b) ((a) < (b) ? (a) : (b))
#else
#define min(a,b)({ __typeof__ (a) _a = (a);__typeof__ (b) _b = (b);_a < _b ? _a : _b; })
#endif

//============================================================
//  PROTOTYPES
//============================================================

int display_get_device_info(const char *screen_option, char *device_name, char *device_id, char *device_key);
int display_get_available_video_modes(const char *device_name, modeline *mode, modeline *current, config_settings *cs);
int display_get_desktop_mode(const char *device_name, modeline *current);
int display_set_desktop_mode(modeline *mode, int flags);
int display_restore_desktop_mode(int flags);

static void set_option_osd(running_machine &machine, const char *option_ID, bool state);
static int copy_to_clipboard(char *txt);



//============================================================
// switchres_init_osd
//============================================================

bool switchres_init_osd(running_machine &machine)
{
	config_settings *cs = &machine.switchres.cs;
	game_info *game = &machine.switchres.game;
	modeline *mode_table = machine.switchres.video_modes;
	modeline *user_mode = &machine.switchres.user_mode;
	monitor_range *range = machine.switchres.range;
	const char * screen, * aspect;
	char resolution[32]={'\x00'};
	modeline desktop_mode;

	windows_options &options = downcast<windows_options &>(machine.options());

	// Initialize structures and config settings
	memset(&desktop_mode, 0, sizeof(struct modeline));
	memset(cs, 0, sizeof(struct config_settings));
	memset(game, 0, sizeof(struct game_info));

	// Init Switchres common info
	switchres_init(machine);

	// Complete config settings
	cs->monitor_count = options.numscreens();
	cs->doublescan = 0;

	// Get device info
	screen = strcmp(options.screen(0), "auto")? options.screen(0) : options.screen();
	display_get_device_info(screen, m_device_name, m_device_id, m_device_key);

	// Get current desktop resolution
	display_get_desktop_mode(m_device_name, &desktop_mode);

	// Initialize custom video
	custom_video_init(m_device_name, m_device_id, &desktop_mode, user_mode, mode_table,
					  options.powerstrip()? CUSTOM_VIDEO_TIMING_POWERSTRIP : 0,
					  options.powerstrip()? (char *)options.ps_timing() : m_device_key);

	// Get per window resolution
	strcpy(resolution, strcmp(options.resolution(0), "auto")? options.resolution(0) : options.resolution());

	// Get list of available video modes
	if (!display_get_available_video_modes(m_device_name, mode_table, &desktop_mode, cs))
	{
		set_option_osd(machine, OSDOPTION_SWITCHRES, false);
		return false;
	}

	// Get per window aspect
	aspect = strcmp(options.aspect(0), "auto")? options.aspect(0) : options.aspect();
	if (strcmp(aspect, "auto"))
	{
		float num, den;
		sscanf(aspect, "%f:%f", &num, &den);
		cs->monitor_aspect = cs->desktop_rotated? den/num : num/den;
	}
	else
		cs->monitor_aspect = STANDARD_CRT_ASPECT;

	// If monitor is LCD, create automatic specs and force resolution
	if (!strcmp(cs->monitor, "lcd"))
	{
		osd_printf_verbose("SwitchRes: Creating automatic specs for LCD based on %s\n", desktop_mode.hactive? "current timings" : "VESA GTF");
		if (!desktop_mode.hactive) modeline_vesa_gtf(&desktop_mode);
		modeline_to_monitor_range(range, &desktop_mode);
		monitor_show_range(range);
		sprintf(resolution, "%dx%d@%d", desktop_mode.width, desktop_mode.height, desktop_mode.refresh);
	}
	// Otherwise (non-LCD), convert the user defined modeline into a -resolution option
	else if (user_mode->hactive)
		sprintf(resolution, "%dx%d", user_mode->hactive, user_mode->vactive);

	// Filter the mode table according the -resolution option
	if (strcmp(resolution, "auto"))
	{
		int i = 1;
		bool found = false;
		osd_printf_verbose("SwitchRes: -resolution was forced as %s\n", resolution);

		if ((sscanf(resolution, "%dx%d@%d", &cs->width, &cs->height, &cs->refresh) < 3) &&
			((!strstr(resolution, "x") || (sscanf(resolution, "%dx%d", &cs->width, &cs->height) != 2))))
				osd_printf_info("SwitchRes: illegal -resolution value: %s\n", resolution);

		else while (mode_table[i].width && i < MAX_MODELINES)
		{
			// Lock all modes that don't match the user's -resolution rules
			if (!( (mode_table[i].width == cs->width || (mode_table[i].type & X_RES_EDITABLE && cs->width <= DUMMY_WIDTH) || cs->width == 0)
				&& (mode_table[i].height == cs->height || cs->height == 0)
				&& (mode_table[i].refresh == cs->refresh || cs->refresh == 0) ))
				mode_table[i].type |= MODE_DISABLED;

			else
			{
				// If we have an user defined modeline, link its label to current item in mode table
				if (user_mode->hactive && !found)
				{
					user_mode->width = mode_table[i].width;
					user_mode->height = mode_table[i].height;
					user_mode->refresh = mode_table[i].refresh;
					user_mode->type = mode_table[i].type & ~V_FREQ_EDITABLE & ~X_RES_EDITABLE;
				}
				mode_table[i].type &= ~MODE_DISABLED;
				mode_table[i].type |= MODE_USER_DEF;
				found = true;
			}
			i++;
		}
		if (!found)
			osd_printf_info("SwitchRes: -resolution value not available: %s\n", resolution);
	}

	// Get game info
	switchres_get_game_info(machine);

	return true;
}

//============================================================
// switchres_modeline_setup
//============================================================

bool switchres_modeline_setup(running_machine &machine)
{
	modeline *best_mode = &machine.switchres.best_mode;
	windows_options &options = downcast<windows_options &>(machine.options());
	windows_osd_interface &osd = downcast<windows_osd_interface &>(machine.osd());
	char modeline_txt[256]={'\x00'};

	osd_printf_verbose("\nSwitchRes: Entering switchres_modeline_setup\n");

	// Find most suitable video mode and generate a modeline for it if we're allowed
	if (!switchres_get_video_mode(machine))
	{
		set_option_osd(machine, OSDOPTION_SWITCHRES, false);
		return false;
	}

	// Make the new video timings available to the system
	if (options.modeline_generation())
	{
		if(!custom_video_update_timing(best_mode))
		{
			set_option_osd(machine, OSDOPTION_SWITCHRES, false);
			return false;
		}

		if (options.verbose())
		{
			modeline_print(best_mode, modeline_txt, MS_FULL);
			copy_to_clipboard(modeline_txt);
		}
	}

	// Set MAME common options
	switchres_set_options(machine);

	// Set MAME OSD specific options

	// Set fullscreen resolution for the OpenGL case
	if (options.switch_res() && (!strcmp(options.video(), "opengl"))) display_set_desktop_mode(best_mode, CDS_FULLSCREEN);

	// Black frame insertion / multithreading
	bool black_frame_insertion = options.black_frame_insertion() && best_mode->result.v_scale > 1 && best_mode->vfreq > 100;
	set_option_osd(machine, OPTION_BLACK_FRAME_INSERTION, black_frame_insertion);

	// Vertical synchronization management (autosync)
	// Disable -syncrefresh if our vfreq is scaled or out of syncrefresh_tolerance (-triplebuffer will be used instead)
	// Forcing -syncrefresh will override the -triplebuffer setting
	bool sync_refresh_effective = black_frame_insertion || !((best_mode->result.weight & R_V_FREQ_OFF) || best_mode->result.v_scale > 1);
	set_option_osd(machine, OPTION_SYNCREFRESH, options.autosync()? sync_refresh_effective : options.sync_refresh());
	set_option_osd(machine, WINOPTION_TRIPLEBUFFER, options.autosync()? !sync_refresh_effective : (options.triple_buffer() && !options.sync_refresh()));
	set_option_osd(machine, OSDOPTION_WAITVSYNC, options.sync_refresh() || options.triple_buffer());

	// Set filter options
	set_option_osd(machine, OSDOPTION_FILTER, ((best_mode->result.weight & R_RES_STRETCH || best_mode->interlace) && (!strcmp(options.video(), "auto") || !strcmp(options.video(), "d3d"))));

	// Refresh video options
	osd.extract_video_config();

	return true;
}

//============================================================
// switchres_modeline_remove
//============================================================

bool switchres_modeline_remove(running_machine &machine)
{
	windows_options &options = downcast<windows_options &>(machine.options());

	// Restore original video timings
	if (options.modeline_generation()) custom_video_restore_timing();
	
	// Set destop resolution for the OpenGL case
	if (options.switch_res() && !strcmp(options.video(), "opengl")) display_restore_desktop_mode(0);

	// Free custom video api
	custom_video_close();

	return true;
}

//============================================================
// switchres_resolution_change
//============================================================

bool switchres_resolution_change(win_window_info *window)
{
	running_machine &machine = window->machine();
	modeline *best_mode = &machine.switchres.best_mode;
	modeline previous_mode;

	// If there's no pending change, just exit
	if (!switchres_check_resolution_change(machine))
		return false;

	// Get the new resolution
	previous_mode = *best_mode;
	switchres_modeline_setup(machine);

	// Only change resolution if the new one is actually different
	if (memcmp(&previous_mode, best_mode, offsetof(modeline, result)))
	{
		window->m_win_config.width = best_mode->width;
		window->m_win_config.height = best_mode->height;
		return true;
	}

	return false;
}

//============================================================
// set_option_osd - option setting wrapper
//============================================================

static void set_option_osd(running_machine &machine, const char *option_ID, bool state)
{
	windows_options &options = downcast<windows_options &>(machine.options());

	options.set_value(option_ID, state, OPTION_PRIORITY_SWITCHRES);
	osd_printf_verbose("SwitchRes: Setting option -%s%s\n", machine.options().bool_value(option_ID)?"":"no", option_ID);
}

//============================================================
// copy_to_clipboard
//============================================================

static int copy_to_clipboard(char *txt)
{
	HGLOBAL hglb;
	hglb = GlobalAlloc(GMEM_MOVEABLE, 256);
	memcpy(GlobalLock(hglb), txt, strlen(txt) + 1);
	GlobalUnlock(hglb);
	OpenClipboard(NULL);
	EmptyClipboard();
	SetClipboardData(CF_TEXT, hglb);
	CloseClipboard();
	return 1;
}
