/**************************************************************

   switchres_module.cpp - Switchres MAME module

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

// MAME headers
#include "emu.h"

// MAMEOS headers
#if defined(OSD_WINDOWS)
#include "winmain.h"
#elif defined(OSD_SDL)
#include "osdsdl.h"
#endif

#include "modules/osdwindow.h"
#include <switchres/switchres.h>
#include "switchres_module.h"


//============================================================
//  switchres_module::init
//============================================================

void switchres_module::init(running_machine &machine)
{
	m_machine = &machine;
	m_switchres = new switchres_manager;

	// Set logging functions
	if (machine.options().verbose()) switchres().set_log_verbose_fn((void *)printf);
	switchres().set_log_info_fn((void *)printf);
	switchres().set_log_error_fn((void *)printf);
}

//============================================================
//  switchres_module::exit
//============================================================

void switchres_module::exit()
{
	osd_printf_verbose("Switchres: exit\n");
	if (m_switchres) delete m_switchres;
	m_switchres = 0;
}

//============================================================
//  switchres_module::exit
//============================================================

display_manager* switchres_module::add_display(int index, osd_monitor_info *monitor, render_target *target, osd_window_config *config)
{
	#if defined(OSD_WINDOWS)
		windows_options &options = downcast<windows_options &>(machine().options());
	#elif defined(OSD_SDL)
		sdl_options &options = downcast<sdl_options &>(machine().options());
	#endif

	switchres().set_screen(monitor->devicename().c_str());
	switchres().set_monitor(options.monitor());
	switchres().set_modeline(options.modeline());
	for (int i = 0; i < MAX_RANGES; i++) switchres().set_crt_range(i, options.crt_range(i));
	switchres().set_modeline_generation(options.modeline_generation());
	switchres().set_lock_unsupported_modes(options.lock_unsupported_modes());
	switchres().set_lock_system_modes(options.lock_system_modes());
	switchres().set_refresh_dont_care(options.refresh_dont_care());

	switchres().set_interlace(options.interlace());
	switchres().set_doublescan(options.doublescan());
	switchres().set_dotclock_min(options.dotclock_min());
	switchres().set_refresh_tolerance(options.sync_refresh_tolerance());
	switchres().set_super_width(options.super_width());

	modeline user_mode = {};
	user_mode.width = config->width;
	user_mode.height = config->height;
	user_mode.refresh = config->refresh;

	display_manager *display = switchres().add_display();
	display->init();
	display->set_rotation(effective_orientation(display, target));
	display->set_user_mode(&user_mode);
	display->set_monitor_aspect(display->desktop_is_rotated()? 1.0f / monitor->aspect() : monitor->aspect());

	int minwidth, minheight;
	target->compute_minimum_size(minwidth, minheight);

	if (display->rotation() ^ display->desktop_is_rotated()) std::swap(minwidth, minheight);
	set_width(index, minwidth);
	set_height(index, minheight);

	// determine the refresh rate of the primary screen
	const screen_device *primary_screen = screen_device_iterator(machine().root_device()).first();
	if (primary_screen != nullptr) set_refresh(index, ATTOSECONDS_TO_HZ(primary_screen->refresh_attoseconds()));

	osd_printf_verbose("Switchres: get_mode(%d) %d %d %f %f\n", index, width(index), height(index), refresh(index), display->monitor_aspect());

	modeline *mode = display->get_mode(width(index), height(index), refresh(index), 0);

	if (mode)
	{
		if (mode->type & MODE_UPDATED) display->update_mode(mode);

		else if (mode->type & MODE_NEW) display->add_mode(mode);

		config->width = mode->width;
		config->height = mode->height;
		config->refresh = mode->refresh;

		if (options.mode_setting())
		{
			display->set_mode(mode);
			monitor->refresh();
		}

		set_options(display, target);
	}

	m_num_screens ++;
	return display;
}


//============================================================
//  switchres_module::get_game_info
//============================================================

void switchres_module::get_game_info()
{
/*
	emu_options &options = m_machine.options();
	game_info *game = &m_machine.switchres.game;
	const game_driver *game_drv = &m_machine.system();
	const screen_device *screen;

	// Get game information
	sprintf(game->name, "%s", options.system_name());
	if (game->name[0] == 0) sprintf(game->name, "empty");

	machine_config config(*game_drv, options);
	screen = screen_device_iterator(config.root_device()).first();

	// Fill in current video mode settings
	game->orientation = effective_orientation();

	if (screen->screen_type() == SCREEN_TYPE_VECTOR)
	{
		game->vector = 1;
		game->width = 640;
		game->height = 480;
	}

	// Output width and height only for games that are not vector
	else
	{
		const rectangle &visarea = screen->visible_area();
		int w = visarea.max_x - visarea.min_x + 1;
		int h = visarea.max_y - visarea.min_y + 1;
		game->width = game->orientation?h:w;
		game->height = game->orientation?w:h;
	}

	game->refresh = ATTOSECONDS_TO_HZ(screen->refresh_attoseconds());

	// Check for multiple screens
	screen_device_iterator iter(config.root_device());
	game->screens = iter.count();
*/
}

//============================================================
//  switchres_module::effective_orientation
//============================================================

bool switchres_module::effective_orientation(display_manager* display, render_target *target)
{

	bool target_is_rotated = (target->orientation() & machine_flags::MASK_ORIENTATION) & ORIENTATION_SWAP_XY? true:false;
	bool game_is_rotated = (machine().system().flags & machine_flags::MASK_ORIENTATION) & ORIENTATION_SWAP_XY;

	return target_is_rotated ^ game_is_rotated ^ display->desktop_is_rotated();
}

//============================================================
//  switchres_module::check_resolution_change
//============================================================

bool switchres_module::check_resolution_change()
{
/*
	game_info *game = &m_machine.switchres.game;
	config_settings *cs = &m_machine.switchres.cs;
	
	int new_width = game->width;
	int new_height = game->height;
	float new_vfreq = game->refresh;
	bool new_orientation = effective_orientation();

	screen_device_iterator scriter(machine.root_device());
	if (scriter.count())
	{
		screen_device *screen = scriter.first();
		if (screen->frame_number())
		{
			const rectangle &visarea = screen->visible_area();
			new_width = new_orientation? visarea.height() : visarea.width();
			new_height = new_orientation? visarea.width() : visarea.height();
			new_vfreq = ATTOSECONDS_TO_HZ(screen->frame_period().m_attoseconds);
		}
	}

	if (game->width != new_width || game->height != new_height || new_vfreq != game->refresh || cs->effective_orientation != new_orientation)
	{
		osd_printf_verbose("SwitchRes: Resolution change from %dx%d@%f %s to %dx%d@%f %s\n",
			game->width, game->height, game->refresh, cs->effective_orientation?"rotated":"normal", new_width, new_height, new_vfreq, new_orientation?"rotated":"normal");

		game->width = new_width;
		game->height = new_height;
		game->refresh = new_vfreq;

		return true;
	}
*/
	return false;
}

//============================================================
//  switchres_module::set_options
//============================================================

void switchres_module::set_options(display_manager* display, render_target *target)
{
	#if defined(OSD_WINDOWS)
		windows_options &options = downcast<windows_options &>(machine().options());
	#elif defined(OSD_SDL)
		sdl_options &options = downcast<sdl_options &>(machine().options());
	#endif

	modeline *best_mode = display->best_mode();

	// Set scaling/stretching options
	set_option(OPTION_KEEPASPECT, true);
	set_option(OPTION_UNEVENSTRETCH, best_mode->result.weight & R_RES_STRETCH);
	set_option(OPTION_UNEVENSTRETCHX, (!(best_mode->result.weight & R_RES_STRETCH) && (best_mode->width >= display->super_width())));

	// Update target if it's already initialized
	if (target)
	{
		if (options.uneven_stretch())
			target->set_scale_mode(SCALE_FRACTIONAL);
		else if(options.uneven_stretch_x())
			target->set_scale_mode(SCALE_FRACTIONAL_X);
		else if(options.uneven_stretch_y())
			target->set_scale_mode(SCALE_FRACTIONAL_Y);
		else
			target->set_scale_mode(SCALE_INTEGER);
	}

	// Black frame insertion / multithreading
	bool black_frame_insertion = options.black_frame_insertion() && best_mode->result.v_scale > 1 && best_mode->vfreq > 100;
	set_option(OSDOPTION_BLACK_FRAME_INSERTION, black_frame_insertion);

	// Set MAME OSD specific options

	// Vertical synchronization management (autosync)
	// Disable -syncrefresh if our vfreq is scaled or out of syncrefresh_tolerance
	bool sync_refresh_effective = black_frame_insertion || !((best_mode->result.weight & R_V_FREQ_OFF) || best_mode->result.v_scale > 1);
	set_option(OSDOPTION_WAITVSYNC, options.autosync()? sync_refresh_effective : options.wait_vsync());
	set_option(OPTION_THROTTLE, options.autosync()? !sync_refresh_effective : options.throttle());
}

//============================================================
//  switchres_module::set_option - option setting wrapper
//============================================================

void switchres_module::set_option(const char *option_ID, bool state)
{
	#if defined(OSD_WINDOWS)
		windows_options &options = downcast<windows_options &>(machine().options());
	#elif defined(OSD_SDL)
		sdl_options &options = downcast<sdl_options &>(machine().options());
	#endif

	//options.set_value(option_ID, state, OPTION_PRIORITY_SWITCHRES);
	options.set_value(option_ID, state, OPTION_PRIORITY_NORMAL+1);
	osd_printf_verbose("SwitchRes: Setting option -%s%s\n", options.bool_value(option_ID)?"":"no", option_ID);
}
