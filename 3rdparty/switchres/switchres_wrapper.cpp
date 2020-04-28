/**************************************************************

   log.h - Simple logging for Switchres

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#define MODULE_API_EXPORTS
#include "switchres.h"
#include "switchres_wrapper.h"
#include "log.h"
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

switchres_manager* swr;


MODULE_API void sr_init() {
	swr = new switchres_manager;
	swr->set_log_verbose_fn((void *)printf);
	swr->set_log_info_fn((void *)printf);
	swr->set_log_error_fn((void *)printf);
	swr->parse_config("switchres.ini");
	swr->add_display();
	for (auto &display : swr->displays)
		display->init();
}


MODULE_API void sr_deinit() {
	delete swr;
}


MODULE_API void sr_set_monitor(const char *preset) {
	swr->set_monitor(preset);
}

void disp_best_mode_to_sr_mode(display_manager* disp, sr_mode* srm)
{
	srm->width = disp->width();
	srm->height = disp->height();
	srm->refresh = disp->refresh();
	srm->is_refresh_off = (disp->is_refresh_off() ? 1 : 0);
	srm->is_stretched = (disp->is_stretched() ? 1 : 0);
	srm->x_scale = disp->x_scale();
	srm->y_scale = disp->y_scale();
	srm->interlace = (disp->is_interlaced() ? 105 : 0);
}


bool sr_refresh_display(display_manager *disp)
{
	if (disp->is_mode_updated())
	{
		if (disp->update_mode(disp->best_mode()))
		{
			log_info("sr_refresh_display: mode was updated\n");
			return true;
		}
	}
	else if (disp->is_mode_new())
	{
		if (disp->add_mode(disp->best_mode()))
		{
			log_info("sr_refresh_display: mode was added\n");
			return true;
		}
	}
	else
	{
		log_info("sr_refresh_display: no refresh required\n");
		return true;
	}

	log_error("sr_refresh_display: error refreshing display\n");
	return false;
}


MODULE_API unsigned char sr_add_mode(int width, int height, double refresh, unsigned char interlace, sr_mode *return_mode) {

	log_verbose("Inside sr_add_mode(%dx%d@%f%s)\n", width, height, refresh, interlace > 0? "i":"");
	display_manager *disp = swr->display();
	if (disp == nullptr)
	{
		log_error("sr_add_mode: error, didn't get a display\n");
		return 0;
	}

	disp->get_mode(width, height, refresh, (interlace > 0? true : false));
	if (disp->got_mode())
	{
		log_verbose("sr_add_mode: got mode %dx%d@%f type(%x)\n", disp->width(), disp->height(), disp->v_freq(), disp->best_mode()->type);
		if (return_mode != nullptr) disp_best_mode_to_sr_mode(disp, return_mode);
		if (sr_refresh_display(disp))
			return 1;
	}

	printf("sr_add_mode: error adding mode\n");
	return 0;
}


MODULE_API unsigned char sr_switch_to_mode(int width, int height, double refresh, unsigned char interlace, sr_mode *return_mode) {

	log_verbose("Inside sr_switch_to_mode(%dx%d@%f%s)\n", width, height, refresh, interlace > 0? "i":"");
	display_manager *disp = swr->display();
	if (disp == nullptr)
	{
		log_error("sr_switch_to_mode: error, didn't get a display\n");
		return 0;
	}

	disp->get_mode(width, height, refresh, (interlace > 0? true : false));
	if (disp->got_mode())
	{
		log_verbose("sr_switch_to_mode: got mode %dx%d@%f type(%x)\n", disp->width(), disp->height(), disp->v_freq(), disp->best_mode()->type);
		if (return_mode != nullptr) disp_best_mode_to_sr_mode(disp, return_mode);
		if (!sr_refresh_display(disp))
			return 0;
	}

	if (disp->is_switching_required())
	{
		if (disp->set_mode(disp->best_mode()))
		{
			log_info("sr_switch_to_mode: successfully switched to %dx%d@%f\n", disp->width(), disp->height(), disp->v_freq());
			return 1;
		}
	}
	else
	{
		log_info("sr_switch_to_mode: switching not required\n");
		return 1;
	}

	log_error("sr_switch_to_mode: error switching to mode\n");
	return 0;
}


MODULE_API srAPI srlib = { 
	sr_init,
	sr_deinit,
	sr_add_mode,
	sr_switch_to_mode
};

#ifdef __cplusplus
}
#endif
