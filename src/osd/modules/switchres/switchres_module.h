/**************************************************************

   switchres_module.h - Switchres MAME module

   ---------------------------------------------------------

   Switchres   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 Chris Kennedy, Antonio Giner,
                         Alexandre Wodarczyk, Gil Delescluse

 **************************************************************/

#ifndef SWITCHRES_MODULE_H_
#define SWITCHRES_MODULE_H_

#define MAX_WINDOWS 4

class osd_window_config;
class switchres_manager;
class display_manager;

class switchres_module
{
public:
	switchres_module() {};
	~switchres_module() {};

	// getters
	running_machine &machine() const { assert(m_machine != nullptr); return *m_machine; }
	switchres_manager &switchres() const { assert(m_switchres != nullptr); return *m_switchres; }
	int width(int i) { return m_width[i]; }
	int height(int i) { return m_height[i]; }
	double refresh(int i) { return m_refresh[i]; }

	// setters
	void set_width(int i, int width) { m_width[i] = width; }
	void set_height(int i, double height) { m_height[i] = height; }
	void set_refresh(int i, double refresh) { m_refresh[i] = refresh; }

	// interface
	void init(running_machine &machine);
	void exit();
	display_manager* add_display(int index, const char* display_name, render_target *target, osd_window_config *config);
	void get_game_info();
	bool effective_orientation();
	bool check_resolution_change();
	void set_options();
	void set_option(const char *option_ID, bool state);

private:
	switchres_manager* m_switchres;
	running_machine*   m_machine;

	int    m_num_screens = 0;

	int    m_width[MAX_WINDOWS];
	int    m_height[MAX_WINDOWS];
	double m_refresh[MAX_WINDOWS];

};

#endif
