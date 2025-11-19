//============================================================
//
//  emusync.h - raster synchronization
//
//============================================================

#pragma once

#ifndef MAME_EMU_SYNC_H
#define MAME_EMU_SYNC_H

#include "expfit.h"

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

	bool osd_init(uint64_t monitor_handle,
					std::function<bool(void)> get_vblank_timestamp,
					std::function<uint64_t(void)> get_frame_counter);
	void osd_deinit();
	bool get_vblank_timestamp_default();
	void reset();
	uint64_t get_tag(enum emusync::event_tag tag);
	void register_tag(enum emusync::event_tag timestamp_event);
	bool register_vblank_in_ticks(uint64_t sync_count, uint64_t timestamp);
	bool register_vblank_in_ns(uint64_t sync_count, uint64_t timestamp);
	void register_emutime(uint64_t emutime);
	void register_sink_samples(int id, uint64_t samples);
	double get_sink_rate(int id);
	uint64_t wait_raster(uint64_t count, double scan);
	void get_raster(raster_status *status);
	void get_scanline(uint32_t *scanline, bool *in_vblank);
	uint64_t period();
	double period_in_ms() { return get_ms(period()); };
	double fd_margin_in_ms() { return get_ms(m_fd_margin); };
	double frame_time_in_ms() { return get_ms(m_frame_time); };
	double current_framedelay();
	uint64_t line_period() { return m_vtotal ? period() / m_vtotal * (m_interlaced ? 2.0 : 1.0) : 0; };
	uint64_t emu_period() { return m_emu_period; };
	double speed_factor() { return handle_throttle() ? (double)m_emu_period / (period() * (1 + m_bfi)) : 1.0; };
	void predraw_sync();
	void postdraw_sync();

	// getters
	running_machine &machine() const noexcept { return m_machine; }

	uint64_t frame_count() const { return m_frame; }
	uint64_t first_sync_count() const { return m_first_sync_count; }
	bool handle_throttle() const { return m_fullscreen && m_syncrefresh; }
	bool sync_refresh() const { return m_syncrefresh; }
	bool sync_audio() const { return m_syncaudio; }
	bool auto_framedelay() const { return m_auto_framedelay; }
	int32_t framedelay() const { return m_framedelay; }
	int32_t vsync_offset() const { return m_vsync_offset; }
	bool interlaced() const { return m_interlaced; }

	// setters
	void set_fullscreen(bool fullscreen) { m_fullscreen = fullscreen; }
	void set_sync_refresh(bool syncrefresh) { m_syncrefresh = syncrefresh; }
	void set_sync_audio(bool syncaudio) { m_syncaudio = syncaudio; }
	void set_framedelay(int framedelay) { m_framedelay = framedelay; }
	void set_fd_margin(float fd_margin) { m_fd_margin = fd_margin * 1e6; } // ms->ns
	void set_auto_framedelay(bool autoframedelay) { m_auto_framedelay = autoframedelay; }
	void set_vsync_offset(int vsync_offset) { m_vsync_offset = vsync_offset; }
	void set_vtotal(int vtotal) { m_vtotal = vtotal; }
	void set_interlace(bool interlace) { m_interlaced = interlace; }

	struct sink_status
	{
		double m_update_ts;
		double m_update_interval;
		uint64_t m_samples_out;
		exp_fit m_ef;

		sink_status(double timestamp, uint64_t samples_out) :
			m_update_ts(timestamp),
			m_update_interval(0.050), // 20 Hz
			m_samples_out(samples_out),
			m_ef(exp_fit(0.025, timestamp, samples_out)) { }
	};

private:
	running_machine &m_machine;

	void update_stats();
	uint64_t time_in_ns();
	inline double get_ms(int64_t time) { return (double)time / 1e6; };
	inline double time_now() { return get_ms(time_in_ns() - m_time_start); };

	bool m_initialized = false;

	uint64_t m_frame = 0;
	uint64_t m_this_sync_frame;
	uint64_t m_next_sync_frame;
	uint64_t m_predraw_sync_wait;
	uint64_t m_postdraw_sync_wait;
	bool     m_missed_previous_retrace;

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
	uint64_t m_emu_period = 0;
	uint64_t m_time_start = 0;

	int64_t  m_vblank_count = 0;
	int64_t  m_current_period = 0;
	int64_t  m_mean = 0;

	bool     m_sleep_allowed;
	bool     m_fullscreen;
	bool     m_syncrefresh;              // flag: TRUE if we're currently refresh-synced
	bool     m_syncaudio;                // flag: TRUE if audio resampling is enabled
	bool     m_auto_framedelay;          // flag: TRUE if automatic frame delay is enabled
	int32_t  m_framedelay;               // tenths of frame to delay emulation start
	uint64_t m_fd_margin;                //
	int32_t  m_vsync_offset;             // offset vsync position by this many lines
	int32_t  m_bfi;
	uint32_t m_vtotal;
	bool     m_interlaced;

	int ticks_to_ns = 0;
	uint64_t sleep_time = 1e6; // 1 ms

	std::function<bool(void)> get_vblank_timestamp;
	std::function<uint64_t(void)> get_frame_counter;

	std::map<uint32_t, struct sink_status> m_sinks;
};
#endif
