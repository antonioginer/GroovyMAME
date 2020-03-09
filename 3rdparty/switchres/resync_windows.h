/**************************************************************

	resync_windows.h - Windows device change notifying helper

	---------------------------------------------------------

	Switchres	Modeline generation engine for emulation

	License     GPL-2.0+
	Copyright   2010-2020 - Chris Kennedy, Antonio Giner

 **************************************************************/

#ifndef __RESYNC_WINDOWS__
#define __RESYNC_WINDOWS__

#include <thread>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <windows.h>

class resync_handler
{
	public:
		resync_handler();
		~resync_handler();

		void wait();

	private:
		static LRESULT CALLBACK resync_wnd_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		LRESULT CALLBACK my_wnd_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		void handler_thread();

		HWND m_hwnd;
		std::thread	my_thread;
		bool m_is_notified;
		std::mutex m_mutex;
		std::condition_variable m_event;
};

#endif
