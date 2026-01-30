// license:BSD-3-Clause
// copyright-holders:Antonio Giner, intealls
//============================================================
//
//  emusync.cpp - raster synchronization
//
//============================================================

#include <atomic>
#include <cstdint>

// MAME headers
#include "emuopts.h"
#include "emusync.h"
#include "screen.h"

#define LOG_VBLANK 0

#if LOG_VBLANK
	#define emusync_printf_verbose(...) osd_printf_verbose(__VA_ARGS__)
	#define emusync_printf_info(...) osd_printf_info(__VA_ARGS__)
#else
	#define emusync_printf_verbose(...)
	#define emusync_printf_info(...)
#endif

#define MAX_PERIOD (1.0 / 49.0) * 1e9
#define MIN_PERIOD (1.0 / 240.0) * 1e9

#define VACTIVE_RATIO_VGA (480.0 / 525.0)
#define VACTIVE_RATIO_CEA (720.0 / 750.0)

//============================================================
//  emusync::emusync
//============================================================

emusync::emusync(running_machine &machine)
	: m_machine(machine)
	, m_emu_period(1e9 / 60)
	, m_time_start(time_in_ns())
	, m_sleep_allowed(machine.options().sleep())
	, m_syncrefresh(machine.options().sync_refresh())
	, m_syncaudio(machine.options().sync_audio())
	, m_auto_framedelay(machine.options().auto_frame_delay())
	, m_framedelay(machine.options().frame_delay())
	, m_fd_margin(machine.options().fd_margin() * 1e6) // ms->ns
	, m_vsync_offset(machine.options().vsync_offset())
	, m_bfi(machine.options().black_frame_insertion())
	, m_emusync_log(machine.options().emusynclog())
	, m_vactive_ratio(VACTIVE_RATIO_VGA)
	, ticks_to_ns(1e9 / osd_ticks_per_second())
	, sleep_time (1 * osd_ticks_per_second() / 1000.0) // 1 ms
	, m_io(asio::io_service())
	, m_serial(m_io)
	, m_serial_read_timer(m_io)
{
	if (*machine.options().emusyncserial())
	{
		try
		{
			m_serial.open(machine.options().emusyncserial());
			m_serial.set_option(asio::serial_port_base::baud_rate(115200));
			serial_write(SERIAL_RESET);
		}
		catch (const std::exception &e)
		{
			osd_printf_error("Error configuring emusync serial port: %s\n", e.what());
		}
	}
};


//============================================================
//  emusync::~emusync
//============================================================

emusync::~emusync()
{
	log_dump();

	if (m_serial.is_open())
		m_serial.close();
};


//============================================================
//  emusync::reset
//============================================================

void emusync::reset()
{
	m_initialized = false;
	m_frame = 0;
	m_first_sync_count = 0;
	m_first_timestamp = 0;
	m_last_sync_count = 0;
	m_last_timestamp = 0;
	m_last_count = 0;

	m_vblank_count = 0;
	m_current_period = 0;
	m_mean = 0;
	m_kf.reset();

	for (auto& [id, sink_st] : m_sinks)
		sink_st.m_reset_request.store(true, std::memory_order_relaxed);
}


//============================================================
//  emusync::compute_vactive_ratio
//============================================================

void emusync::compute_vactive_ratio()
{
	// We don't have active video information, pick a reasonable default
	if (vactive() == 0)
		m_vactive_ratio = VACTIVE_RATIO_VGA;

	// We have active video information but no vtotal, pick an usual ratio
	else if (vtotal() == 0)
		m_vactive_ratio = vactive() > 480 ? VACTIVE_RATIO_CEA : VACTIVE_RATIO_VGA;

	// We have full information (this should be the case)
	else
		m_vactive_ratio = (double)vactive() / (double)vtotal();

	osd_printf_verbose("emusync: vactive: %d vtotal: %d m_vactive_ratio: %f\n", vactive(), vtotal(), m_vactive_ratio);
}


//============================================================
//  emusync::time_in_ns
//============================================================

inline uint64_t emusync::time_in_ns()
{
	struct timespec monotime;
	clock_gettime(CLOCK_MONOTONIC, &monotime);
	return (uint64_t)(monotime.tv_sec) * (uint64_t)1000000000 + (uint64_t)(monotime.tv_nsec);
}


//============================================================
//  emusync::get_tag
//============================================================

uint64_t emusync::get_tag(enum emusync::event_tag tag)
{
	return m_timestamp[tag] / ticks_to_ns;
}


//============================================================
//  emusync::register_tag
//============================================================

void emusync::register_tag(enum emusync::event_tag tag)
{
	// Register tag
	uint64_t prev_timestamp = m_timestamp[tag];
	m_timestamp[tag] = time_in_ns();

	switch ((int)tag)
	{
		case BEFORE_DRAW:
		{
			if (m_timestamp[AFTER_DRAW] != 0)
				register_emutime(m_timestamp[BEFORE_DRAW] - m_timestamp[AFTER_DRAW]);
			break;
		}

		case AFTER_DRAW:
		{
			if (prev_timestamp != 0)
			{
				m_frame_time = m_timestamp[tag] - prev_timestamp;
				emusync_printf_verbose("present: %.3f emu_t: %.3f emu_t_avg: %.3f Dm: %.3f period: %.3f\n\n",
					get_ms(m_timestamp[AFTER_PRESENT] - m_timestamp[BEFORE_PRESENT]), get_ms(m_current_emulation_time), get_ms(m_emulation_time_avg), get_ms(m_emulation_time_dm), frame_time_in_ms());
			}
			else
				emusync_printf_verbose("\n");

			update_stats();
			log_register_work_items();
			break;
		}
	}
}


//============================================================
//  emusync::register_emutime
//============================================================

void emusync::register_emutime(uint64_t emutime)
{
	static int i = 0;
	static int regs = 0;
	const int max_regs = sizeof(m_emulation_time) / sizeof(m_emulation_time[0]);
	osd_ticks_t acum = 0;
	int diff = 0;

	// Discard invalid values
	if (emutime <= 0)
		return;

	log("emusync::register_emutime [ylim(0.0:0.024)]", NOW, (double)emutime / 1e9);

	// Register value and compute current average
	m_current_emulation_time = emutime;
	m_emulation_time[i] = emutime;
	i++;

	if (i > max_regs)
		i = 0;

	if (regs < max_regs)
		regs++;

	for (int k = 0; k < regs; k++)
		acum += m_emulation_time[k];

	m_emulation_time_avg = acum / regs;

	// Compute current max deviation
	osd_ticks_t max_diff = 0;

	for (int k = 1; k <= regs; k++)
	{
		diff = m_emulation_time[k] - m_emulation_time[k-1];

		if (diff > 0 && diff > max_diff)
			max_diff = diff;
	}

	int diff_delta = (max_diff - m_emulation_time_dm) / 16;
	m_emulation_time_dm += diff_delta;
}


//============================================================
//  emusync::register_sink_samples
//============================================================

void emusync::register_sink_samples(uint32_t id, uint64_t samples)
{
	double timestamp = time_now() / 1e3;

	auto [sink_st, inserted] = m_sinks.try_emplace(id);

	if (inserted || sink_st->second.m_reset_request.load(std::memory_order_relaxed)) {
		sink_st->second.reset(timestamp);
		return;
	}

	sink_st->second.m_samples_out += samples;

	if (timestamp - sink_st->second.m_update_ts >= sink_st->second.m_update_interval) {
		sink_st->second.m_ef.update(timestamp, sink_st->second.m_samples_out);
		sink_st->second.m_update_ts = timestamp;
	}
}


//============================================================
//  emusync::sink_rate
//============================================================

double emusync::get_sink_rate(int id)
{
	auto sink_st = m_sinks.find(id);

	if (sink_st == m_sinks.end())
		return 0.0;

	return sink_st->second.m_ef.slope_out();
}


//============================================================
//  emusync::register_vblank_in_ticks
//============================================================

bool emusync::register_vblank_in_ticks(uint64_t sync_count, uint64_t timestamp)
{
	return register_vblank_in_ns(sync_count, timestamp * ticks_to_ns);
}


//============================================================
//  emusync::register_vblank_in_ns
//============================================================

bool emusync::register_vblank_in_ns(uint64_t sync_count, uint64_t timestamp)
{
	int64_t delta;
	int count_delta = 0;

	emusync_printf_verbose("[%.3f] register vblank: ", time_now());

	log("emusync::register_vblank_in_ns [diff]", NOW, (double)(timestamp) / 1e9);
	log("emusync::register_vblank_in_ns (sync_count) [diff]", NOW, sync_count);

	if (m_initialized)
	{
		count_delta = sync_count - m_last_sync_count;

		// Skip sample if it's not newer.
		if (count_delta == 0)
			goto register_and_exit;

		// Sanity check for jumps in count_delta. These unfortunately happen.
		if (count_delta < 0 || count_delta > 10)
		{
			emusync_printf_verbose("count delta: %d -> reset!\n", count_delta);
			reset();
			return false;
		}

		// Sometimes the received counter is not properly incremented.
		// This breaks period computation. So we recalculate it based on the timestamp.
		if (m_mean > 0)
			count_delta = round(double(timestamp - m_last_timestamp) / (double)m_mean);

		// Computed delta below 1 here means corrupted timestamps (e.g. by some overlay)
		if (count_delta < 1)
		{
			osd_printf_verbose("count delta error!\n");
			return false;
		}

		// Compute current period
		m_current_period = (timestamp - m_last_timestamp) / count_delta;

		// Final sanity check for computed period
		if (m_current_period < MIN_PERIOD || m_current_period > MAX_PERIOD * (interlaced() ? 2.0 : 1.0))
		{
			emusync_printf_verbose("period out of range: %f ms\n", get_ms(m_current_period));
			return false;
		}

		// Filter timestamp. If needed, compute intermediate timestamps to feed the filter.
		if (m_kf.initialized)
		{
			for (int i = count_delta; i > 0; --i) m_kf.predict();
			m_kf.update(timestamp);
		}
		else
			m_kf.init(timestamp, (1.0 / 60.0) * 1e9);

		//emusync_printf_verbose("raw: %lld filtered: %lld diff: %+d period: %f\n", timestamp, m_kf.get_filtered_timestamp(),
		//					(int64_t)(timestamp - m_kf.get_filtered_timestamp()), get_ms(m_kf.get_period()));

		delta = m_current_period - m_mean;

		m_vblank_count++;
		m_mean += delta / m_vblank_count;
		emusync_printf_verbose("[%.3f] sync: %d, period: %f, diff: %+f ms, mean: %f ms",
			get_ms(timestamp - m_first_timestamp), sync_count - m_first_sync_count, get_ms(m_current_period), get_ms(delta), get_ms(m_mean));

		log("Kalman filter period [median(-8e-6:8e-6)]", NOW, (double)(m_kf.get_period()) / 1e9);
	}

	if (!m_initialized)
	{
		emusync_printf_verbose("initialize, sync_count %d", sync_count);
		m_initialized = true;
		m_first_sync_count = sync_count;
		m_first_timestamp = timestamp;
	}

register_and_exit:

	if(count_delta != 1)
		emusync_printf_verbose(" count_delta: %d\n", count_delta);
	else
		emusync_printf_verbose("\n");

	m_last_count = sync_count - m_first_sync_count;
	m_last_sync_count = sync_count;
	m_last_timestamp = timestamp;

	return count_delta > 0;
}


//============================================================
//  emusync::wait_raster
//============================================================

uint64_t emusync::wait_raster(uint64_t count, double scan)
{
	//uint64_t sync_target = m_last_timestamp + vsync_offset() * line_period() + (count - m_last_count) * period();
	uint64_t sync_target = m_kf.get_filtered_timestamp() + vsync_offset() * line_period() + (count - m_last_count) * period();
	uint64_t time_target = sync_target + (uint64_t)(scan * period());

	uint64_t time_entry = time_in_ns();

	emusync_printf_verbose("wait raster [%d][%.3f]: ", count, scan);

	// Wait for target time
	if ((int)(time_target - time_entry) > 0)
	{
		emusync_printf_verbose("must wait: %+.3f ", get_ms(time_target) - get_ms(time_entry));

		uint64_t current_time;
		do
		{
			current_time = time_in_ns();
			if (current_time >= time_target)
				break;

			if (m_sleep_allowed && (time_target - current_time) > 2e6) // 2 ms
				osd_sleep(sleep_time);

		} while ((current_time - time_entry) < period() * 2);
	}
	else
		emusync_printf_verbose("delayed, exiting. ");

	uint64_t time_exit = time_in_ns();
	emusync_printf_verbose("elapsed: %.3f\n", get_ms(time_exit - time_entry));

	return time_exit - time_entry;
}


//============================================================
//  emusync::get_raster
//============================================================

void emusync::get_raster(raster_status *status)
{
	if (status == nullptr)
		return;

	//uint64_t adjusted_prev_timestamp = m_last_timestamp + vsync_offset() * line_period();
	uint64_t adjusted_prev_timestamp = m_kf.get_filtered_timestamp() + vsync_offset() * line_period();

	int64_t delta_time = time_in_ns() - adjusted_prev_timestamp;

	status->count = (double)m_last_count + (double)delta_time / period();
	status->scan = (double)(time_in_ns() - (adjusted_prev_timestamp + (status->count - m_last_count) * period())) / period();
}


//============================================================
//  emusync::get_raster
//============================================================

double emusync::current_framedelay()
{
	uint64_t effective_margin = std::max(m_emulation_time_dm, m_fd_margin);
	uint64_t adjusted_emulation_time = std::min(m_emulation_time_avg + effective_margin, period());
	return std::max((double)(period() - adjusted_emulation_time) / period(), 0.0);
}


//============================================================
//  emusync::update_stats
//============================================================

void emusync::update_stats()
{
	// determine the refresh rate of the primary screen
	const screen_device *primary_screen = screen_device_enumerator(machine().root_device()).first();

	if (primary_screen != nullptr && primary_screen->configured())
		m_emu_period = primary_screen->frame_period().as_attoseconds() / 1e9;
}


//============================================================
//  emusync::predraw_sync
//============================================================

void emusync::predraw_sync()
{
	register_tag(emusync::BEFORE_DRAW);

	m_predraw_sync_wait = 0;
	raster_status raster = {};

	if (get_vblank_timestamp == nullptr)
		goto exit;

	if (m_frame == 0)
		goto exit;

	if (!get_vblank_timestamp())
	{
		osd_printf_verbose("emusync: get_vblank_timestamp() error!\n");
		goto exit;
	}

	get_raster(&raster);
	emusync_printf_verbose("[%.3f] get raster->[%d][%.3f] ", time_now(), raster.count, raster.scan);

	m_this_sync_frame = raster.count;
	m_missed_previous_retrace = m_this_sync_frame > m_next_sync_frame;

	if (handle_throttle() && machine().video().throttled() && !m_missed_previous_retrace)
		m_predraw_sync_wait = wait_raster(raster.count, m_vactive_ratio);
	else
		emusync_printf_verbose("missed retrace\n");

	exit:
	register_tag(emusync::BEFORE_PRESENT);
	serial_write(emusync::BEFORE_PRESENT);
}


//============================================================
//  emusync::postdraw_sync
//============================================================

void emusync::postdraw_sync()
{
	register_tag(emusync::AFTER_PRESENT);
	serial_write(emusync::AFTER_PRESENT);

	m_postdraw_sync_wait = 0;

	if (get_frame_counter == nullptr)
		m_frame++;
	else
		m_frame = get_frame_counter();

	if (m_frame == 1)
		goto exit;

	double fd;
	if (machine().options().auto_frame_delay() && m_framedelay == 0)
		// automatic
		fd = current_framedelay();
	else
		// user defined
		fd = (double)(m_framedelay) / 10.0;

	log("Frame delay", NOW, (double) fd * 10.0);

	m_next_sync_frame = m_this_sync_frame + (m_missed_previous_retrace? 0 : 1);

	if (handle_throttle() && machine().video().throttled())
	{
		emusync_printf_verbose("[%.3f] ", time_now());
		m_postdraw_sync_wait = wait_raster(m_next_sync_frame, fd);
	}

	emusync_printf_verbose("[%.3f] wait: %.3f ", time_now(), get_ms(m_predraw_sync_wait + m_postdraw_sync_wait));

	exit:
	register_tag(emusync::AFTER_DRAW);
}


//============================================================
//  emusync::log
//============================================================

void emusync::log(std::string tag, log_type type, double value)
{
	if (!(m_emusync_log && m_machine.options().seconds_to_run()))
		return;

	double timestamp = time_now() / 1e3;
	auto work_item = m_log_work_items.find(tag);

	if (work_item == m_log_work_items.end())
	{
		m_log_work_items.emplace(tag, log_work_item(timestamp, value, type));
		m_log_out_vectors.emplace(tag, log_out_vector(m_machine.options().seconds_to_run() * 60));
		return;
	}

	work_item->second.update(timestamp, value);
}


//============================================================
//  emusync::log_register_work_items
//============================================================

void emusync::log_register_work_items()
{
	for (auto &work_pair : m_log_work_items)
	{
		auto out_vector = m_log_out_vectors.find(work_pair.first);
		out_vector->second.save(work_pair.second.get_result());
	}
}


//============================================================
//  emusync::log_dump
//============================================================

void emusync::log_dump()
{
	for (const auto &out_pair : m_log_out_vectors)
	{
		osd_printf_info("EMUSYNC LOG (%d items): %s\n", out_pair.second.m_count, out_pair.first);
		for (int i = 0; i < out_pair.second.m_count; i++)
			osd_printf_info("%.9f,%.9f\n", out_pair.second.m_out_items[i].m_timestamp, out_pair.second.m_out_items[i].m_value);
		osd_printf_info("\n");
	}
}


//============================================================
//  emusync::serial_write
//============================================================

bool emusync::serial_write(uint8_t msg)
{
	if (m_serial.is_open())
	{
		const uint8_t send[1] = { msg };
		asio::error_code ec;

		asio::write(m_serial, asio::buffer(send), ec);

		if (ec)
		{
			osd_printf_error("Error writing to emusync serial port: %s\n", ec.message());
			return false;
		}
		return true;
	}
	return false;
}


//============================================================
//  emusync::serial_exchange
//============================================================

bool emusync::serial_exchange(uint8_t msg, uint8_t* rdbuf, int count)
{
	if (m_serial.is_open())
	{
		m_io.restart();

		m_serial_read_timer.expires_after(std::chrono::seconds(1));
		m_serial_read_timer.async_wait(
			[this](const asio::error_code& ec)
			{
				if (!ec)
				{
					osd_printf_error("Emusync serial read timeout, disabling serial port\n");

					m_serial.cancel();
					m_serial.close();
				}
			}
		);

		asio::async_read(m_serial, asio::buffer(rdbuf, count),
			[this](const asio::error_code& ec, std::size_t bytes)
			{
				if (ec)
					osd_printf_error("Emusync serial read error: %s\n", ec.message());

				m_serial_read_timer.cancel();
			}
		);

		asio::async_write(m_serial, asio::buffer(&msg, 1),
			[this](const asio::error_code& ec, std::size_t bytes)
			{
				if (ec)
					osd_printf_error("Emusync serial write error: %s\n", ec.message());
			}
		);

		m_io.run();
	}

	return m_serial.is_open();
}


//============================================================
//  emusync::serial_dump
//============================================================

void emusync::serial_collect()
{
	uint8_t rxcnt;
	uint8_t rx[127] = { 0 };

	if (!serial_exchange(SERIAL_FREEZE, &rxcnt, 1)) return;

	if (rxcnt < sizeof(serial_header_t) || rxcnt > sizeof(serial_header_t) + 10 * sizeof(serial_tag_t) ||
	    (rxcnt - sizeof(serial_header_t)) % sizeof(serial_tag_t))
	{
		osd_printf_error("Emusync serial: invalid receive count %u\n", rxcnt);
		return;
	}

	if (!serial_exchange(SERIAL_DUMP, rx, rxcnt)) return;

	serial_header_t* header = reinterpret_cast<serial_header_t*>(rx);

	int system_clock = header->system_clock;
	int vsync_count = header->vsync_count;
	int vsync_timestamp = header->vsync_timestamp;
	int prev_vsync_timestamp = header->prev_vsync_timestamp;

	int frame_time = vsync_timestamp - prev_vsync_timestamp;

	if (frame_time <= 0)
	{
		osd_printf_error("Emusync serial: invalid frame time %d\n", frame_time);
		return;
	}

	int n_tags = (rxcnt - sizeof(serial_header_t)) / sizeof(serial_tag_t);
	serial_tag_t* tags = reinterpret_cast<serial_tag_t*>(rx + sizeof(serial_header_t));

	if (!m_machine.options().emusynclog())
	{
		osd_printf_info("[%.3f][%s] %u, %.3f Hz:", time_now(), m_machine.options().emusyncserial(), vsync_count, (double) system_clock / (vsync_timestamp - prev_vsync_timestamp));

		if (!n_tags)
			osd_printf_info(" no tags\n");
	}

	for (int i = 0; i < n_tags; i++)
	{
		uint32_t tag = tags[i].data;
		uint32_t timestamp = tags[i].timestamp;

		// try and align to when we sent the tag, not when it was received (1 start bit, 8 data bits)
		int vsync_uart_diff = (int)(timestamp - 9 * (system_clock / 115200)) - prev_vsync_timestamp;

		auto tag_label = event_tag_map.find((event_tag) tag);
		const char* label = tag_label != event_tag_map.end() ? tag_label->second : "UNKNOWN";

		if (!m_machine.options().emusynclog())
		{
			osd_printf_info(" %s @ %7.3f%%", label, ((double) vsync_uart_diff / frame_time) * 100.f);
			osd_printf_info("%s", i != n_tags - 1 ? "," : "\n");
		}
		else
		{
			std::stringstream ss;
			ss << std::string(tag_label->second) << " [ylim(-200:200)]";
			log(ss.str(), NOW, ((double) vsync_uart_diff / frame_time) * 100.f);
		}
	}
}
