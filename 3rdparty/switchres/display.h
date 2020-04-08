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
	char   modeline[256];
	char   crt_range[MAX_RANGES][256];
	char   lcd_range[256];

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
	int index() const { return m_index; }
	bool desktop_is_rotated() const { return m_desktop_is_rotated; }

	// getters (display manager)
	const char *set_monitor() { return (const char*) &m_ds.monitor; }
	const char *user_modeline() { return (const char*) &m_ds.modeline; }
	const char *crt_range(int i) { return (const char*) &m_ds.crt_range[i]; }
	const char *lcd_range() { return (const char*) &m_ds.lcd_range; }
	const char *screen() { return (const char*) &m_ds.screen; }
	const char *api() { return (const char*) &m_ds.api; }
	bool modeline_generation() { return m_ds.modeline_generation; }
	bool lock_unsupported_modes() { return m_ds.lock_unsupported_modes; }
	bool lock_system_modes() { return m_ds.lock_system_modes; }
	bool refresh_dont_care() { return m_ds.refresh_dont_care; }
	const char *ps_timing() { return (const char*) &m_ds.ps_timing; }

	// getters (modeline generator)
	bool interlace() { return m_ds.gs.interlace; }
	bool doublescan() { return m_ds.gs.doublescan; }
	double dotclock_min() { return m_ds.gs.pclock_min; }
	double refresh_tolerance() { return m_ds.gs.refresh_tolerance; }
	int super_width() { return m_ds.gs.super_width; }
	bool rotation() { return m_ds.gs.rotation; }
	double monitor_aspect() { return m_ds.gs.monitor_aspect; }
	int v_shift_correct() { return m_ds.gs.v_shift_correct; }

	// setters
	void set_factory(custom_video *factory) { m_factory = factory; }
	void set_custom_video(custom_video *video) { m_video = video; }
	void set_user_mode(modeline *mode) { m_user_mode = *mode; filter_modes(); }
	void set_index(int index) { m_index = index; }
	void set_desktop_is_rotated(bool value) { m_desktop_is_rotated = value; }
	void set_rotation(bool value) { m_ds.gs.rotation = value; }
	void set_monitor_aspect(float aspect) { m_ds.gs.monitor_aspect = aspect; }
	void set_v_shift_correct(int value) { m_ds.gs.v_shift_correct = value; }

	// options
	display_settings m_ds = {};

	// mode setting interface
	modeline *get_mode(int width, int height, float refresh, bool interlaced);
	bool add_mode(modeline *mode);
	bool delete_mode(modeline *mode);
	bool update_mode(modeline *mode);
	virtual bool set_mode(modeline *);
	void log_mode(modeline *mode);

	// mode list handling
	bool filter_modes();
	bool restore_modes();
	bool auto_specs();

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

	int m_index = 0;
	bool m_desktop_is_rotated = 0;
};

#endif
