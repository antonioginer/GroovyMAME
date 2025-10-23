//============================================================
//
//  emusync.h - raster synchronization
//
//============================================================

#pragma once

#ifndef __EMU_H__
#error Dont include this file directly; include emu.h instead.
#endif

#ifndef MAME_EMU_SYNC_H
#define MAME_EMU_SYNC_H

class emusync
{
public:

	emusync(running_machine &machine);
	~emusync() {};

	enum event_tag
	{
		BEFORE_DRAW,
		AFTER_DRAW,
		BEFORE_PRESENT,
		AFTER_PRESENT,
		TIMESTAMP_ITEMS
	};

	struct raster_status
	{
		uint64_t count;
		double scan;
	};

	void reset();
	uint64_t get_tag(enum emusync::event_tag tag);
	void register_tag(enum emusync::event_tag timestamp_event);
	bool register_vblank_in_ticks(uint64_t sync_count, uint64_t timestamp);
	bool register_vblank_in_ns(uint64_t sync_count, uint64_t timestamp);
	void register_emutime(uint64_t emutime);
	uint64_t wait_raster(uint64_t count, double scan);
	void get_raster(raster_status *status);
	uint64_t period() { return m_mean > 0? (uint64_t)m_mean : 1e9 / 60; };
	double period_in_ms() { return get_ms(period()); };
	double fd_margin_in_ms() { return get_ms(m_fd_margin); };
	double frame_time_in_ms() { return get_ms(m_frame_time); };
	double current_framedelay();

	// getters
	running_machine &machine() const noexcept { return m_machine; }

	bool sync_refresh() const { return m_syncrefresh; }
	bool sync_audio() const { return m_syncaudio; }
	bool auto_framedelay() const { return m_auto_framedelay; }
	int32_t framedelay() const { return m_framedelay; }
	int32_t vsync_offset() const { return m_vsync_offset; }

	// setters
	void set_sync_refresh(bool syncrefresh) { m_syncrefresh = syncrefresh; }
	void set_sync_audio(bool syncaudio) { m_syncaudio = syncaudio; }
	void set_framedelay(int framedelay) { m_framedelay = framedelay; }
	void set_fd_margin(float fd_margin) { m_fd_margin = fd_margin * 1e6; } // ms->ns
	void set_auto_framedelay(bool autoframedelay) { m_auto_framedelay = autoframedelay; }
	void set_vsync_offset(int vsync_offset) { m_vsync_offset = vsync_offset; }


private:
	running_machine &m_machine;

	inline double get_ms(int64_t time) { return (double)time / 1e6; };
	inline uint64_t time_in_ns() { return osd_ticks() * ticks_to_ns; };
/*	inline uint64_t time_in_ns()
	{
		struct timespec monotime;
		clock_gettime(CLOCK_MONOTONIC, &monotime);
		return (uint64_t)(monotime.tv_sec) * (uint64_t)1000000000 + (uint64_t)(monotime.tv_nsec);
	}
*/
	bool m_initialized = false;

	// All timestamps in nanoseconds
	uint64_t m_timestamp[static_cast<int>(TIMESTAMP_ITEMS)] {};
	uint64_t m_first_sync_count = 0;
	uint64_t m_first_timestamp = 0;
	uint64_t m_last_sync_count = 0;
	uint64_t m_last_timestamp = 0;
	uint64_t m_last_count = 0;

	uint64_t m_current_emulation_time;
	uint64_t m_emulation_time[16] = {};
	uint64_t m_emulation_time_avg = 0;
	uint64_t m_emulation_time_dm = 0;
	uint64_t m_frame_time = 0;

	int64_t  m_vblank_count = 0;
	int64_t  m_current_period = 0;
	int64_t  m_mean = 0;

	bool     m_sleep_allowed;
	bool     m_syncrefresh;              // flag: TRUE if we're currently refresh-synced
	bool     m_syncaudio;                // flag: TRUE if audio resampling is enabled
	bool     m_auto_framedelay;          // flag: TRUE if automatic frame delay is enabled
	int32_t  m_framedelay;               // tenths of frame to delay emulation start
	uint64_t m_fd_margin;                //
	int32_t  m_vsync_offset;             // offset vsync position by this many lines

	int ticks_to_ns = 0;
	uint64_t sleep_time = 1e6; // 1 ms
};
#endif
