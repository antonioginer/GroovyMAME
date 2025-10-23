//============================================================
//
//  emusync.cpp - raster synchronization
//
//============================================================

// MAME headers
#include "emu.h"
#include "emuopts.h"
#include "emusync.h"

#define GPU_IS_DCN 1
#if !defined(OSD_WINDOWS) && GPU_IS_DCN
	#define VBLANK_OFFSET 1.0e6 // hack
#else
	#define VBLANK_OFFSET 0
#endif


//============================================================
//  emusync::emusync
//============================================================

emusync::emusync(running_machine &machine)
	: m_machine(machine)
	, m_sleep_allowed(machine.options().sleep())
	, m_syncrefresh(machine.options().sync_refresh())
	, m_syncaudio(machine.options().sync_audio())
	, m_auto_framedelay(machine.options().auto_frame_delay())
	, m_framedelay(machine.options().frame_delay())
	, m_fd_margin(machine.options().fd_margin() * 1e6) // ms->ns
	, m_vsync_offset(machine.options().vsync_offset())
	, ticks_to_ns(1e9 / osd_ticks_per_second())
	, sleep_time (1 * osd_ticks_per_second() / 1000.0) // 1 ms
{
};

//============================================================
//  emusync::reset
//============================================================

void emusync::reset()
{
	m_initialized = false;
	m_first_sync_count = 0;
	m_first_timestamp = 0;
	m_last_sync_count = 0;
	m_last_timestamp = 0;
	m_last_count = 0;

	m_vblank_count = 0;
	m_current_period = 0;
	m_mean = 0;
}


//============================================================
//  emusync::register_tag
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
				uint64_t present_time = m_timestamp[AFTER_PRESENT] - m_timestamp[BEFORE_PRESENT];
				osd_printf_verbose("present: %.3f emu_t: %.3f emu_t_avg: %.3f Dm: %.3f period: %.3f\n\n",
					get_ms(present_time), get_ms(m_current_emulation_time), get_ms(m_emulation_time_avg), get_ms(m_emulation_time_dm), frame_time_in_ms());
			}
			else
				osd_printf_verbose("\n");
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

	osd_printf_verbose("register vblank: ");

	if (m_initialized)
	{
		count_delta = sync_count - m_last_sync_count;

		// Skip sample if it's not newer. Big deltas may be inaccurate, discard.
		if (count_delta == 0 || count_delta > 4)
		{
			osd_printf_verbose("count delta: %d\n", count_delta);
			goto register_and_exit;
		}

		// Sometimes the received counter is not properly incremented.
		// This breaks period computation. So we recalculate it based on the timestamp.
		if (m_mean > 0)
			count_delta = round(double(timestamp - m_last_timestamp) / (double)m_mean);

		// Double check we have a valid delta now
		if (count_delta == 0)
		{
			osd_printf_verbose("count delta: %d\n", count_delta);
			goto register_and_exit;
		}

		m_current_period = (timestamp - m_last_timestamp) / count_delta;

		if (m_current_period > 20 * 1e6) // 20 ms
		{
			osd_printf_verbose("invalid period: %.3f ms\n", get_ms(m_current_period));
			goto register_and_exit;
		}

		delta = m_current_period - m_mean;

		m_vblank_count++;
		m_mean += delta / m_vblank_count;
		osd_printf_verbose("[%.3f] sync: %d, period: %f, diff: %+f ms, mean: %f ms\n",
			get_ms(timestamp - m_first_timestamp), sync_count - m_first_sync_count, get_ms(m_current_period), get_ms(delta), get_ms(m_mean));
	}

	if (!m_initialized)
	{
		osd_printf_verbose("initialize, sync_count %d\n", sync_count);
		m_initialized = true;
		m_first_sync_count = sync_count;
		m_first_timestamp = timestamp;
	}

register_and_exit:
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
	uint64_t sync_target = m_last_timestamp - VBLANK_OFFSET + (count - m_last_count) * period();
	uint64_t time_target = sync_target + (uint64_t)(scan * period());

	uint64_t time_entry = time_in_ns();

	osd_printf_verbose("wait raster [%d][%.3f]: ", count, scan);

	// Wait for target time
	if ((int)(time_target - time_entry) > 0)
	{
		osd_printf_verbose("must wait: %+.3f ", get_ms(time_target) - get_ms(time_entry));

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
		osd_printf_verbose("delayed, exiting. ");

	uint64_t time_exit = time_in_ns();
	osd_printf_verbose("elapsed: %.3f\n", get_ms(time_exit - time_entry));

	return time_exit - time_entry;
}


//============================================================
//  emusync::get_raster
//============================================================

void emusync::get_raster(raster_status *status)
{
	if (status == nullptr)
		return;

	uint64_t adjusted_prev_timestamp = m_last_timestamp - VBLANK_OFFSET;

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
