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
//  switchres_module::add_display
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
	switchres().set_lcd_range(options.lcd_range());
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
	display->set_user_mode(&user_mode);
	display->init();
	display->set_monitor_aspect(display->desktop_is_rotated()? 1.0f / monitor->aspect() : monitor->aspect());

	get_game_info(display, target);

	osd_printf_verbose("Switchres: get_mode(%d) %d %d %f %f\n", index, width(index), height(index), refresh(index), display->monitor_aspect());
	modeline *mode = display->get_mode(width(index), height(index), refresh(index), 0);
	if (mode != nullptr) set_mode(index, monitor, target, config);

	m_num_screens ++;
	return display;
}


//============================================================
//  switchres_module::get_game_info
//============================================================

void switchres_module::get_game_info(display_manager* display, render_target *target)
{
	display->set_rotation(effective_orientation(display, target));

	int minwidth, minheight;
	target->compute_minimum_size(minwidth, minheight);

	if (display->rotation() ^ display->desktop_is_rotated()) std::swap(minwidth, minheight);
	set_width(display->index(), minwidth);
	set_height(display->index(), minheight);

	// determine the refresh rate of the primary screen
	const screen_device *primary_screen = screen_device_iterator(machine().root_device()).first();
	if (primary_screen != nullptr) set_refresh(display->index(), ATTOSECONDS_TO_HZ(primary_screen->refresh_attoseconds()));
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

bool switchres_module::check_resolution_change(int i, osd_monitor_info *monitor, render_target *target, osd_window_config *config)
{
	display_manager *display = switchres().display(i);

	int old_width = width(i);
	int old_height = height(i);
	double old_refresh = refresh(i);
	bool old_rotation = display->rotation();

	get_game_info(display, target);

	if (old_width != width(i) || old_height != height(i) || old_refresh != refresh(i) || old_rotation != display->rotation())
	{
		osd_printf_verbose("Switchres: Resolution change from %dx%d@%f %s to %dx%d@%f %s\n",
			old_width, old_height, old_refresh, old_rotation?"rotated":"normal", width(i), height(i), refresh(i), display->rotation()?"rotated":"normal");

		modeline old_mode = *display->best_mode();
		modeline *mode = display->get_mode(width(i), height(i), refresh(i), 0);

		if (mode != nullptr)
		{
			if (memcmp(mode, &old_mode, sizeof(modeline) - sizeof(mode_result)) != 0)
			{
				set_mode(i, monitor, target, config);
				return true;
			}

			set_options(display, target);
		}
	}

	return false;
}

//============================================================
//  switchres_module::set_mode
//============================================================

bool switchres_module::set_mode(int i, osd_monitor_info *monitor, render_target *target, osd_window_config *config)
{
	#if defined(OSD_WINDOWS)
		windows_options &options = downcast<windows_options &>(machine().options());
	#elif defined(OSD_SDL)
		sdl_options &options = downcast<sdl_options &>(machine().options());
	#endif

	display_manager *display = switchres().display(i);
	modeline *mode = display->best_mode();

	if (mode != nullptr)
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

		return true;
	}

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

	#if defined(OSD_WINDOWS)
		downcast<windows_osd_interface &>(machine().osd()).extract_video_config();
	#elif defined(OSD_SDL)
		downcast<sdl_osd_interface &>(machine().osd()).extract_video_config();
	#endif
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
