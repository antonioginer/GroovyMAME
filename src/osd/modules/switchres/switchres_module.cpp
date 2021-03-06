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
//  logging wrappers
//============================================================

static void sr_printf_verbose(const char *format, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	osd_vprintf_verbose(util::make_format_argument_pack(std::forward<char*>(buffer)));
	va_end(args);
}

static void sr_printf_info(const char *format, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	osd_vprintf_info(util::make_format_argument_pack(std::forward<char*>(buffer)));
	va_end(args);
}

static void sr_printf_error(const char *format, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	osd_vprintf_error(util::make_format_argument_pack(std::forward<char*>(buffer)));
	va_end(args);
}

//============================================================
//  switchres_module::init
//============================================================

void switchres_module::init(running_machine &machine)
{
	m_machine = &machine;
	m_switchres = new switchres_manager;

	// Set logging functions
	if (machine.options().verbose()) switchres().set_log_verbose_fn((void *)sr_printf_verbose);
	switchres().set_log_info_fn((void *)sr_printf_info);
	switchres().set_log_error_fn((void *)sr_printf_error);
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
	switchres().set_v_shift_correct(options.v_shift_correct());

	switchres().set_api(options.switchres_backend());
	switchres().set_screen_compositing(options.screen_compositing());
	switchres().set_screen_reordering(options.screen_reordering());
	switchres().set_allow_hardware_refresh(options.allow_hw_refresh());

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
	display->get_mode(width(index), height(index), refresh(index), 0);
	if (display->got_mode()) set_mode(index, monitor, target, config);

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
	const screen_device *primary_screen = screen_device_enumerator(machine().root_device()).first();
	if (primary_screen != nullptr) set_refresh(display->index(), primary_screen->frame_number() == 0? ATTOSECONDS_TO_HZ(primary_screen->refresh_attoseconds()) : primary_screen->frame_period().as_hz());
	//if (primary_screen != nullptr) set_refresh(display->index(), primary_screen->frame_period().as_hz());
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

		display->get_mode(width(i), height(i), refresh(i), 0);

		if (display->got_mode())
		{
			if (display->is_switching_required())
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

	if (display->got_mode())
	{
		if (display->is_mode_updated()) display->update_mode(display->best_mode());

		else if (display->is_mode_new()) display->add_mode(display->best_mode());

		config->width = display->width();
		config->height = display->height();
		config->refresh = display->refresh();

		if (options.mode_setting())
		{
			display->set_mode(display->best_mode());
			monitor->refresh();
			monitor->update_resolution(display->width(), display->height());
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

	// Set scaling/stretching options
	set_option(OPTION_KEEPASPECT, true);
	set_option(OPTION_UNEVENSTRETCH, display->is_stretched());
	set_option(OPTION_UNEVENSTRETCHX, (!(display->is_stretched()) && (display->width() >= display->super_width())));

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

	// Set MAME OSD specific options

	// Vertical synchronization management (autosync)
	// Disable -syncrefresh if our vfreq is scaled or out of syncrefresh_tolerance
	bool sync_refresh_effective = (options.black_frame_insertion() > 0) || !((display->is_refresh_off()) || display->v_scale() > 1);
	set_option(OSDOPTION_WAITVSYNC, options.autosync()? sync_refresh_effective : options.wait_vsync());
	set_option(OPTION_SYNCREFRESH, options.autosync()? sync_refresh_effective : options.sync_refresh());

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

//============================================================
//  switchres_module::mode_to_txt
//============================================================

const char *switchres_module::display_mode_to_txt(int i)
{
	if (!downcast<osd_options &>(machine().options()).switch_res())
		return "Switchres is disabled\n";

	display_manager *display = switchres().display(i);

	if (display == nullptr)
		sprintf(m_mode_txt, "SR(%d): no physical display\n", i);

	else if (display->got_mode())
		sprintf(m_mode_txt, "SR(%d): %d x %d%s%s %2.3f Hz %2.3f kHz\n",
				i, display->width(), display->height(), display->is_interlaced()?"i":"p", display->is_doublescanned()?"d":"", display->v_freq(), display->h_freq()/1000);
	else
		sprintf(m_mode_txt, "SR(%d): could not find a video mode\n", i);

	return m_mode_txt;
}
