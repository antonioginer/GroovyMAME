// MAME headers
#include "emu.h"
#include "emuopts.h"

#include "raster_sync.h"


//============================================================
//  raster_sync::raster_sync
//============================================================

raster_sync::raster_sync()
		: ticks_to_ns(1e9 / osd_ticks_per_second())
		, sleep_time (1 * osd_ticks_per_second() / 1000.0) // 1 ms
	{};

//============================================================
//  raster_sync::reset
//============================================================

void raster_sync::reset()
{
	m_initialized = false;
	m_first_sync_count = 0;
	m_first_timestamp = 0;
	m_last_sync_count = 0;
	m_last_timestamp = 0;

	m_vblank_count = 0;
	m_current_period = 0;
	m_mean = 0;
}


//============================================================
//  raster_sync::register_tag
//============================================================

uint64_t raster_sync::get_tag(enum raster_sync::event_tag tag)
{
	return m_timestamp[tag] / ticks_to_ns;
}

//============================================================
//  raster_sync::register_tag
//============================================================

void raster_sync::register_tag(enum raster_sync::event_tag tag)
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
			//update_stats();
			if (prev_timestamp != 0)
			{
				m_frame_time = m_timestamp[tag] - prev_timestamp;
				uint64_t present_time = m_timestamp[AFTER_PRESENT] - m_timestamp[BEFORE_PRESENT];
				osd_printf_verbose("present: %.3f emu_t: %.3f emu_t_avg: %.3f Dm: %.3f period: %.3f\n\n",
					get_ms(present_time), get_ms(m_current_emulation_time), get_ms(m_emulation_time_avg), get_ms(m_emulation_time_dm), frame_time_in_ms());
				if (fabs(frame_time_in_ms() - period_in_ms()) > 1.0)
					osd_printf_info("glitch!\n");
			}
			break;
		}
	}
}


//============================================================
//  raster_sync::register_emutime
//============================================================

void raster_sync::register_emutime(uint64_t emutime)
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
//  raster_sync::register_vblank_in_ticks
//============================================================

bool raster_sync::register_vblank_in_ticks(uint64_t sync_count, uint64_t timestamp)
{
	register_vblank_in_ns_rls(sync_count, timestamp * ticks_to_ns);
	return register_vblank_in_ns(sync_count, timestamp * ticks_to_ns);
}


//============================================================
//  raster_sync::register_vblank_in_ns
//============================================================

bool raster_sync::register_vblank_in_ns(uint64_t sync_count, uint64_t timestamp)
{
	int64_t delta;
	int count_delta = 0;

	osd_printf_verbose("register vblank: ");

	if (m_initialized)
	{
		count_delta = sync_count - m_last_sync_count;

		// Skip sample if it's not newer
		if (count_delta > 0)
		{
			//register_vblank_in_ns_rls(sync_count, timestamp);
			// Sometimes the received counter is not properly incremented.
			// This breaks period computation. So we recalculate it based on the timestamp.

			if (m_mean > 0)
				count_delta = round(double(timestamp - m_last_timestamp) / (double)m_mean);

			m_current_period = (timestamp - m_last_timestamp) / count_delta;
			delta = m_current_period - m_mean;

			m_vblank_count++;
			m_mean += delta / m_vblank_count;
			osd_printf_verbose("[%.3f] sync: %d, period: %f, diff: %+f ms, mean: %f ms\n",
				get_ms(timestamp - m_first_timestamp), sync_count - m_first_sync_count, get_ms(m_current_period), get_ms(delta), get_ms(m_mean));
/*
			// Fix me
			if (m_vblank_count % 600 == 0)
				m_initialized = false;
*/
		}
		else
			osd_printf_verbose("count delta: %d\n", count_delta);
	}

	if (!m_initialized)
	{
		osd_printf_verbose("initialize, sync_count %d\n", sync_count);
		m_initialized = true;
		m_first_sync_count = sync_count;
		m_first_timestamp = timestamp;
	}

	m_last_sync_count = sync_count;
	m_last_timestamp = timestamp;

	return count_delta > 0;
}


//============================================================
//  raster_sync::register_vblank_in_ns
//============================================================

void raster_sync::register_vblank_in_ns_rls(uint64_t sync_count, uint64_t timestamp)
{
	if (!initialized)
	{
		first_sync_count = sync_count;
		first_timestamp = timestamp;
	}

	last_sync_count = sync_count;
	last_timestamp = timestamp;

	int n = sync_count - first_sync_count;
	double y = double(timestamp - first_timestamp);

	// Vector de regresores: [1, n]^T
	double x1 = 1.0;
	double x2 = (double)n;

	// Predicción actual
	double y_hat = t0 * x1 + P * x2;

	// Error de predicción
	double error = y - y_hat;

	// Ganancia de Kalman (2x1)
	double denom = (P11 * x1 + P12 * x2) * x1 + (P21 * x1 + P22 * x2) * x2 + 1.0;
	double k1 = (P11 * x1 + P12 * x2) / denom;
	double k2 = (P21 * x1 + P22 * x2) / denom;

	// Actualización de parámetros
	t0 += k1 * error;
	P  += k2 * error;

	// Actualización de covarianza
	double P11_new = P11 - k1 * (P11 * x1 + P12 * x2);
	double P12_new = P12 - k1 * (P12 * x1 + P22 * x2);
	double P21_new = P21 - k2 * (P11 * x1 + P12 * x2);
	double P22_new = P22 - k2 * (P21 * x1 + P22 * x2);

	P11 = P11_new;
	P12 = P12_new;
	P21 = P21_new;
	P22 = P22_new;

	initialized = true;
	k = n;
}

//============================================================
//  raster_sync::wait_raster
//============================================================

uint64_t raster_sync::wait_raster(uint64_t count, double scan)
{
	uint64_t sync_target = m_first_timestamp + count * period();
	uint64_t time_target = sync_target + (uint64_t)(scan * period());

	if (initialized)
	{
		sync_target = first_timestamp + (int)t0 + (uint64_t)(count * P);
		time_target = sync_target + (uint64_t)(scan * P);
	}

	uint64_t time_entry = time_in_ns();

    osd_printf_verbose("t0: %.3f P: %.3f target: %.3f entry: %.4f\n", t0, P, time_target, time_entry);

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
//  raster_sync::get_raster
//============================================================

void raster_sync::get_raster(raster_status *status)
{
	if (status == nullptr)
		return;

//	status->count = (time_in_ns() - m_first_timestamp) / period();
//	status->scan = (double)(time_in_ns() - (m_first_timestamp + status->count * period())) / period();

osd_printf_verbose("time_in_ns: %lld first_timestamp: %lld\n", time_in_ns(), first_timestamp);
	status->count = (uint64_t)(time_in_ns() - (first_timestamp + (int)t0)) / P;
	status->scan = (double)(time_in_ns() - (first_timestamp + t0 + status->count * P)) / P;

}


//============================================================
//  raster_sync::get_raster
//============================================================

double raster_sync::auto_framedelay()
{
	uint64_t effective_margin = std::max(m_emulation_time_dm, m_fd_margin);
	uint64_t adjusted_emulation_time = std::min(m_emulation_time_avg + effective_margin, period());
	return std::max((double)(period() - adjusted_emulation_time) / period(), 0.0);
}
