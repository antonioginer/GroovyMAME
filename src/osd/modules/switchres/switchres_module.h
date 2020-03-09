#ifndef SWITCHRES_MODULE_H_
#define SWITCHRES_MODULE_H_

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

	void init(running_machine &machine);
	void exit();
	display_manager* add_display(const char* display_name, int width, int height, int refresh, float aspect);
	void get_game_info();
	bool effective_orientation();
	bool check_resolution_change();
	void set_options();
	void set_option(const char *option_ID, bool state);

private:
	switchres_manager* m_switchres;
	running_machine*   m_machine;

};

#endif