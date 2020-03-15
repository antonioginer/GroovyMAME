#include <stdio.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <getopt.h>
#include "switchres.h"
#include "log.h"

using namespace std;

const string WHITESPACE = " \n\r\t\f\v";
int show_version();
int show_usage();
double get_aspect(const char* aspect);

//============================================================
//  File parsing helpers
//============================================================

string ltrim(const string& s)
{
	size_t start = s.find_first_not_of(WHITESPACE);
	return (start == string::npos) ? "" : s.substr(start);
}

string rtrim(const string& s)
{
	size_t end = s.find_last_not_of(WHITESPACE);
	return (end == string::npos) ? "" : s.substr(0, end + 1);
}

string trim(const string& s)
{
	return rtrim(ltrim(s));
}

bool get_value(const string& line, string& key, string& value)
{
	size_t key_end = line.find_first_of(WHITESPACE);
	
	key = line.substr(0, key_end);
	value = ltrim(line.substr(key_end + 1));
	
	if (key.length() > 0 && value.length() > 0)
		return true;

	return false;
}

constexpr unsigned int s2i(const char* str, int h = 0)
{
    return !str[h] ? 5381 : (s2i(str, h+1)*33) ^ str[h];
}


//============================================================
//  parse_config
//============================================================

bool parse_config(switchres_manager &switchres, const char *file_name)
{	
	log_verbose("parsing %s\n", file_name);

	ifstream config_file(file_name);

	if (!config_file.is_open())
		return false;
	
	string line;
	while (getline(config_file, line))
	{		
		line = trim(line);
		if (line.length() == 0 || line.at(0) == '#')
			continue;

		string key, value;
		if(get_value(line, key, value))
		{
			switch (s2i(key.c_str()))
			{
				// Switchres options
				case s2i("monitor"):
					transform(value.begin(), value.end(), value.begin(), ::tolower);
					switchres.set_monitor(value.c_str());
					break;
				case s2i("orientation"):
					switchres.set_orientation(value.c_str());
					break;
				case s2i("crt_range0"):
					switchres.set_crt_range(0, value.c_str());
					break;
				case s2i("crt_range1"):
					switchres.set_crt_range(1, value.c_str());
					break;
				case s2i("crt_range2"):
					switchres.set_crt_range(2, value.c_str());
					break;
				case s2i("crt_range3"):
					switchres.set_crt_range(3, value.c_str());
					break;
				case s2i("crt_range4"):
					switchres.set_crt_range(4, value.c_str());
					break;
				case s2i("crt_range5"):
					switchres.set_crt_range(5, value.c_str());
					break;
				case s2i("crt_range6"):
					switchres.set_crt_range(6, value.c_str());
					break;
				case s2i("crt_range7"):
					switchres.set_crt_range(7, value.c_str());
					break;
				case s2i("crt_range8"):
					switchres.set_crt_range(8, value.c_str());
					break;
				case s2i("crt_range9"):
					switchres.set_crt_range(9, value.c_str());
					break;
				case s2i("lcd_range"):
					switchres.set_lcd_range(value.c_str());
					break;

				// Display options
				case s2i("display"):
					switchres.set_screen(value.c_str());
					break;
				case s2i("api"):
					switchres.set_api(value.c_str());
					break;
				case s2i("modeline_generation"):
					switchres.set_modeline_generation(atoi(value.c_str()));
					break;
				case s2i("lock_unsupported_modes"):
					switchres.set_lock_unsupported_modes(atoi(value.c_str()));
					break;
				case s2i("lock_system_modes"):
					switchres.set_lock_system_modes(atoi(value.c_str()));
					break;
				case s2i("refresh_dont_care"):
					switchres.set_refresh_dont_care(atoi(value.c_str()));
					break;
				case s2i("ps_timing"):
					switchres.set_ps_timing(value.c_str());
					break;

				// Modeline generation options
				case s2i("interlace"):
					switchres.set_interlace(atoi(value.c_str()));
					break;
				case s2i("doublescan"):
					switchres.set_doublescan(atoi(value.c_str()));
					break;
				case s2i("dotclock_min"):
				{
					double pclock_min = 0.0f;
					sscanf(value.c_str(), "%lf", &pclock_min);
					switchres.set_dotclock_min(pclock_min);
					break;
				}
				case s2i("sync_refresh_tolerance"):
				{
					double refresh_tolerance = 0.0f;
					sscanf(value.c_str(), "%lf", &refresh_tolerance);
					switchres.set_refresh_tolerance(refresh_tolerance);
					break;
				}
				case s2i("super_width"):
				{
					int super_width = 0;
					sscanf(value.c_str(), "%d", &super_width);
					switchres.set_super_width(super_width);
					break;
				}
				case s2i("aspect"):
					switchres.set_monitor_aspect(get_aspect(value.c_str()));
					break;

				default:
					cout << "Invalid option " << key << '\n';
					break;
			}
		}
	}
	config_file.close();
	return true;
}


//============================================================
//  main
//============================================================

int main(int argc, char **argv)
{

	switchres_manager switchres;

	// Init logging
	switchres.set_log_info_fn((void*)printf);
	switchres.set_log_error_fn((void*)printf);

	parse_config(switchres, "switchres.ini");

	int width = 0;
	int height = 0;
	float refresh = 0.0;
	modeline user_mode = {};

	int version_flag = false;
	bool verbose_flag = false;
	bool help_flag = false;
	bool resolution_flag = false;
	bool calculate_flag = false;
	bool switch_flag = false;
	bool launch_flag = false;
	bool rotated_flag = false;
	bool force_flag = false;
	bool interlaced_flag = false;
	bool user_ini_flag = false;

	string ini_file;
	string launch_command;

	while (1)
	{
		static struct option long_options[] =
		{
			{"version",     no_argument,       &version_flag, '1'},
			{"help",        no_argument,       0, 'h'},
			{"calc",        no_argument,       0, 'c'},
			{"switch",      no_argument,       0, 's'},
			{"launch",      required_argument, 0, 'l'},
			{"monitor",     required_argument, 0, 'm'},
			{"orientation", required_argument, 0, 'o'},
			{"aspect",      required_argument, 0, 'a'},
			{"rotated",     no_argument,       0, 'r'},
			{"display",     required_argument, 0, 'd'},
			{"force",       required_argument, 0, 'f'},
			{"ini",         required_argument, 0, 'i'},
			{"verbose",     no_argument,       0, 'v'},
			{0, 0, 0, 0}
		};

		int option_index = 0;
		int c = getopt_long(argc, argv, "vhcsl:m:o:a:rd:f:i:", long_options, &option_index);

		if (c == -1)
			break;

		switch (c)
		{
			case 'v':
				verbose_flag = true;
				break;

			case 'h':
				help_flag = true;
				break;

			case 'c':
				calculate_flag = true;
				break;

			case 's':
				switch_flag = true;
				break;

			case 'l':
				launch_flag = true;
				launch_command = optarg;
				break;

			case 'm':
				switchres.set_monitor(optarg);
				break;

			case 'o':
				switchres.set_orientation(optarg);
				break;

			case 'r':
				rotated_flag = true;
				break;

			case 'd':
				switchres.set_screen(optarg);
				break;

			case 'a':
				switchres.set_monitor_aspect(get_aspect(optarg));
				break;

			case 'f':
				force_flag = true;
				if (sscanf(optarg, "%dx%d@%d", &user_mode.width, &user_mode.height, &user_mode.refresh) < 1)
					log_error("Error: use format --force <w>x<h>@<r>\n");
				break;

			case 'i':
				user_ini_flag = true;
				ini_file = optarg;
				break;

			default:
				return 0;
		}
	}

	if (verbose_flag)
	{
		switchres.set_log_verbose_fn((void*)printf);
	}

	if (version_flag)
	{
		show_version();
		return 0;
	}

	if (help_flag)
		goto usage;

	// Get user video mode information from command line
	if ((argc - optind) < 3)
	{
		log_error("Error: missing argument\n");
		goto usage;
	}
	else if ((argc - optind) > 3)
	{
		log_error("Error: too many arguments\n");
		goto usage;
	}
	else
	{
		resolution_flag = true;
		width = atoi(argv[optind]);
		height = atoi(argv[optind + 1]);
		refresh = atof(argv[optind + 2]);

		char scan_mode = argv[optind + 2][strlen(argv[optind + 2]) -1];
		if (scan_mode == 'i')
			interlaced_flag = true;
	}

	if (user_ini_flag)
		parse_config(switchres, ini_file.c_str());

	switchres.init();

	if (force_flag)
		switchres.display()->set_user_mode(&user_mode);
	
	if (!calculate_flag)
		switchres.display()->init();

	if (resolution_flag)
	{
		modeline *mode = switchres.display()->get_mode(width, height, refresh, interlaced_flag, rotated_flag);
		if (mode)
		{
			if (mode->type & MODE_UPDATED)
			{
				switchres.display()->update_mode(mode);
			}
			else if (mode->type & MODE_NEW)
			{
				switchres.display()->add_mode(mode);
			}

			if (switch_flag)
			{
				switchres.display()->set_mode(mode);
				if (!launch_flag)
				{
					log_info("Press ENTER to exit...\n");
					cin.get();
				}
			}

			if (launch_flag)
			{
				int status_code = system(launch_command.c_str());
				log_info("Process exited with value %d\n", status_code);
			}
		}
	}

	return (0);

usage:
	show_usage();
	return 0;
}

//============================================================
//  get_aspect
//============================================================

double get_aspect(const char* aspect)
{
	int num, den;
	if (sscanf(aspect, "%d:%d", &num, &den) == 2)
	{
		if (den == 0)
		{
			log_error("Error: denominator can't be zero\n");
			return STANDARD_CRT_ASPECT;
		}
		return (double(num)/double(den));
	}

	log_error("Error: use format --aspect <num:den>\n");
	return STANDARD_CRT_ASPECT;
}

//============================================================
//  show_version
//============================================================

int show_version()
{
	char version[]
	{
		"Switchres " SWITCHRES_VERSION "\n"
		"Modeline generation engine for emulation\n"
		"Copyright (C) 2010-2019 - Chris Kennedy, Antonio Giner\n"
		"License GPL-2.0+\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n"
	};

	log_info("%s", version);
	return 0;
}

//============================================================
//  show_usage
//============================================================

int show_usage()
{
	char usage[] =
	{
		"Usage: switchres <width> <height> <refresh> [options]\n"
		"Options:\n"
		"  -c, --calc                        Calculate video mode and exit\n"
		"  -s, --switch                      Switch to video mode\n"
		"  -l, --launch <command>            Launch <command>\n"
		"  -m, --monitor <preset>            Monitor preset (generic_15, arcade_15, pal, ntsc, etc.)\n"
		"  -o, --orientation <orientation>   Monitor orientation (horizontal, vertical, rotate_r, rotate_l)\n"
		"  -a  --aspect <num:den>            Monitor aspect ratio\n"
		"  -r  --rotated                     Original mode's native orientation is rotated\n"
		"  -d, --display <OS_display_name>   Use target display (Windows: \\\\.\\DISPLAY1, ... Linux: VGA-0, ...)\n"
		"  -f, --force <w>x<h>@<r>           Force a specific video mode from display mode list\n"
		"  -i, --ini <file.ini>              Specify a ini file\n"
	};

	log_info("%s", usage);
	return 0;
}
