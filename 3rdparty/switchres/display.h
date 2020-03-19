/**************************************************************

   display.h - Display manager

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include <vector>
#include "modeline.h"
#include "custom_video.h"

typedef struct display_settings
{
	char   screen[32];
	char   api[32];
	bool   modeline_generation;
	bool   lock_unsupported_modes;
	bool   lock_system_modes;
	bool   refresh_dont_care;
	char   ps_timing[256];

	char   monitor[32];
	char   orientation[32];
	char   modeline[256];
	char   crt_range[MAX_RANGES][256];
	char   lcd_range[256];
	bool   monitor_rotates_cw;

	generator_settings gs;
} display_settings;


class display_manager
{
public:

	display_manager() {};
	virtual ~display_manager()
	{
		restore_modes();
		if (m_factory) delete m_factory;
	};

	display_manager *make(display_settings *ds);
	void parse_options();
	virtual bool init();
	virtual int caps();

	// getters
	custom_video *factory() const { return m_factory; }
	custom_video *video() const { return m_video; }
	modeline user_mode() const { return m_user_mode; }
	modeline *best_mode() const { return m_best_mode; }

	// setters
	void set_user_mode(modeline *mode) { m_user_mode = *mode; }
	void set_factory(custom_video *factory) { m_factory = factory; }
	void set_custom_video(custom_video *video) { m_video = video; }

	// options
	display_settings m_ds = {};
	bool m_desktop_rotated;

	// mode setting interface
	modeline *get_mode(int width, int height, float refresh, bool interlaced, bool rotated);
	bool add_mode(modeline *mode);
	bool delete_mode(modeline *mode);
	bool update_mode(modeline *mode);
	virtual bool set_mode(modeline *);
	void log_mode(modeline *mode);

	// mode list handling
	bool filter_modes();
	bool restore_modes();

	// mode list
	std::vector<modeline> video_modes = {};
	std::vector<modeline> backup_modes = {};
	modeline desktop_mode = {};

	// monitor preset
	monitor_range range[MAX_RANGES];

private:

	// custom video backend
	custom_video *m_factory = 0;
	custom_video *m_video = 0;

	modeline m_user_mode = {};
	modeline *m_best_mode = 0;
};

#endif
