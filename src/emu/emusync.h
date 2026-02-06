// license:BSD-3-Clause
// copyright-holders:Antonio Giner, intealls
//============================================================
//
//  emusync.h - raster synchronization
//
//============================================================

#pragma once

#ifndef MAME_EMU_SYNC_H
#define MAME_EMU_SYNC_H

#include <atomic>
#include <asio.h>

#include "expfit.h"
#include "kalman.h"

#include <map>

class emusync
{
public:

	emusync(running_machine &machine);
	~emusync();

	enum event_tag
	{
		BEFORE_SYNC = 0,
		AFTER_SYNC,
		BEFORE_BLIT,
		AFTER_BLIT,
		POLL_INPUT,
		TIMESTAMP_ITEMS
	};

	std::unordered_map<event_tag, const char*> event_tag_map =
	{
		{ BEFORE_SYNC,     "BEFORE_SYNC"  },
		{ AFTER_SYNC,      "AFTER_SYNC"   },
		{ BEFORE_BLIT,     "BEFORE_BLIT"  },
		{ AFTER_BLIT,      "AFTER_BLIT"   },
		{ POLL_INPUT,      "POLL_INPUT"   },
	};

	enum serial_command
	{
		SERIAL_FREEZE = 240,
		SERIAL_DUMP,
		SERIAL_RESET
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
	void register_sink_samples(uint32_t id, uint64_t samples);
	double get_sink_rate(int id);
	uint64_t wait_raster(uint64_t count, double scan);
	void get_raster(raster_status *status);
	void get_scanline(uint32_t *scanline, bool *in_vblank);
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
	uint64_t period() {	return m_vblank_count > 10 ? m_kf.get_period() : 1e9 / 60; }
	double period_in_ms() { return get_ms(period()); };
	double fd_margin_in_ms() { return get_ms(m_fd_margin); };
	double frame_time_in_ms() { return get_ms(m_frame_time); };
	double current_framedelay();
	uint32_t vactive() { return m_vactive != 0 ? m_vactive : m_vactive_osd; };	// Priorize values are set directly through set_vratio.
	uint32_t vtotal() { return m_vtotal != 0 ? m_vtotal : m_vtotal_osd; }		// Otherwise use data from osd when available.
	uint64_t line_period() { return vtotal() != 0 ? period() / vtotal() * (m_interlaced ? 2.0 : 1.0) : 0; };
	uint64_t emu_period() { return m_emu_period; };
	double speed_factor() { return handle_throttle() ? (double)m_emu_period / (period() * (1 + m_bfi)) : 1.0; };

	// setters
	void set_fullscreen(bool fullscreen) { m_fullscreen = fullscreen; }
	void set_sync_refresh(bool syncrefresh) { m_syncrefresh = syncrefresh; }
	void set_sync_audio(bool syncaudio) { m_syncaudio = syncaudio; }
	void set_framedelay(int framedelay) { m_framedelay = framedelay; }
	void set_fd_margin(float fd_margin) { m_fd_margin = fd_margin * 1e6; } // ms->ns
	void set_auto_framedelay(bool autoframedelay) { m_auto_framedelay = autoframedelay; }
	void set_vsync_offset(int vsync_offset) { m_vsync_offset = vsync_offset; }
	void set_vratio(int vactive, int vtotal) { m_vactive = vactive; m_vtotal = vtotal; compute_vactive_ratio(); }
	void set_interlace(bool interlace) { m_interlaced = interlace; }

	enum log_type
	{
		NOW
	};

	void log(std::string tag, log_type type, double value);

	typedef struct
	{
		uint8_t data;
		uint32_t timestamp;
	} __attribute__((packed)) serial_tag_t;

	typedef struct
	{
		uint32_t system_clock;
		uint32_t vsync_count;
		uint32_t vsync_timestamp;
		uint32_t prev_vsync_timestamp;
	} __attribute__((packed)) serial_header_t;

	bool serial_write(uint8_t msg);
	void serial_collect();

private:
	running_machine &m_machine;

	void update_stats();
	void compute_vactive_ratio();
	uint64_t time_in_ns();
	inline double get_ms(int64_t time) { return (double)time / 1e6; };
	inline double time_now() { return get_ms(time_in_ns() - m_time_start); };

	kalman_filter m_kf;

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
	bool     m_emusync_log;
	uint32_t m_vactive = 0;
	uint32_t m_vactive_osd = 0;
	uint32_t m_vtotal = 0;
	uint32_t m_vtotal_osd = 0;
	bool     m_interlaced;
	double   m_vactive_ratio;

	int ticks_to_ns = 0;
	uint64_t sleep_time = 1e6; // 1 ms

	std::function<bool(void)> get_vblank_timestamp;
	std::function<uint64_t(void)> get_frame_counter;

	struct sink_status
	{
		double m_update_ts;
		double m_update_interval;
		uint64_t m_samples_out;
		exp_fit m_ef;
		std::atomic<bool> m_reset_request;

		sink_status() :
			m_update_ts(0.0),
			m_update_interval(0.050), // 20 Hz
			m_samples_out(0),
			m_ef(0.025),
		 	m_reset_request(false) { }

		void reset(double timestamp) {
			m_update_ts = timestamp;
			m_samples_out = 0;
			m_ef.reset();
			m_ef.update(m_update_ts, m_samples_out);
			m_reset_request.store(false, std::memory_order_relaxed);
		}
	};

	std::map<uint32_t, struct sink_status> m_sinks;

	struct log_out_item
	{
		double m_timestamp;
		double m_value;

		log_out_item(double timestamp, double value) :
			m_timestamp(timestamp), m_value(value) { };
	};

	struct log_work_item
	{
		log_type m_type;
		log_out_item m_item;
		int m_n;

		void update(double timestamp, double value)
		{
			m_item.m_timestamp = timestamp;

			if (m_n == 0)
				m_item.m_value = value;
			else
				switch(m_type)
				{
				case NOW:
				default:
					m_item.m_value = value;
					break;
				}

			m_n++;
		}

		log_out_item& get_result()
		{
			switch(m_type)
			{
			case NOW:
			default:
				m_n = 0;
				return m_item;
			}
		}

		log_work_item(double timestamp, double value, log_type type) :
			m_type(type), m_item(timestamp, value), m_n(0) { };
	};

	struct log_out_vector
	{
		int m_max_count;
		int m_count;
		std::vector<log_out_item> m_out_items;

		void save(const log_out_item& i)
		{
			if (m_count < m_max_count)
			{
				log_out_item& item = m_out_items[m_count++];
				item.m_timestamp = i.m_timestamp;
				item.m_value = i.m_value;
			}
		}

		log_out_vector(int max_count) : m_max_count(max_count), m_count(0)
		{
			m_out_items.reserve(max_count);
		}
	};

	std::map<std::string, log_work_item> m_log_work_items;
	std::map<std::string, log_out_vector> m_log_out_vectors;

	void log_register_work_items();
	void log_dump();

	asio::io_service  m_io;
	asio::serial_port m_serial;
	asio::steady_timer m_serial_read_timer;

	bool serial_exchange(uint8_t msg, uint8_t* rdbuf, int length);
};
#endif
