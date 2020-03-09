/**************************************************************

	resync_windows.cpp - Windows device change notifying helper

	---------------------------------------------------------

	Switchres	Modeline generation engine for emulation

	License     GPL-2.0+
	Copyright   2010-2020 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include <functional>
#include "resync_windows.h"
#include "log.h"

//============================================================
//  resync_handler::resync_handler
//============================================================

resync_handler::resync_handler()
{
	my_thread = std::thread(std::bind(&resync_handler::handler_thread, this));
}

//============================================================
//  resync_handler::~resync_handler
//============================================================

resync_handler::~resync_handler()
{
	SendMessage(m_hwnd, WM_CLOSE, 0, 0);
	my_thread.join();
}

//============================================================
//  resync_handler::handler_thread
//============================================================

void resync_handler::handler_thread()
{
	WNDCLASSEX wc;
	MSG msg;
	HINSTANCE hinst = GetModuleHandle(NULL);

	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = this->resync_wnd_proc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.cbWndExtra = 0;
	wc.cbClsExtra = 0;
	wc.hInstance = hinst;
	wc.hbrBackground = 0;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = "resync_handler";
	wc.hIcon = NULL;
	wc.hIconSm = wc.hIcon;
	wc.hCursor = LoadCursor(NULL, IDC_HAND);

	RegisterClassEx(&wc);

	m_hwnd = CreateWindowEx(0, "resync_handler", NULL, WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, hinst, NULL);
	SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

//============================================================
//  resync_handler::wait
//============================================================

void resync_handler::wait()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_is_notified = false;

	auto start = std::chrono::steady_clock::now();

	while (!m_is_notified)
		m_event.wait_for(lock, std::chrono::milliseconds(1000));

	auto end = std::chrono::steady_clock::now();
	log_verbose("resync time elapsed %I64d ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count());
}

//============================================================
//  resync_handler::resync_wnd_proc
//============================================================

LRESULT CALLBACK resync_handler::resync_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	resync_handler *me = reinterpret_cast<resync_handler*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (me) return me->my_wnd_proc(hwnd, msg, wparam, lparam);

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK resync_handler::my_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_DEVICECHANGE:
		{
			m_is_notified = true;
			m_event.notify_one();
			return 0;
		}
		break;

		case WM_CLOSE:
		{
			PostQuitMessage(0);
			return 0;
		}

		default:
			return DefWindowProc(hwnd, msg, wparam, lparam);
	}
	return 0;
}
