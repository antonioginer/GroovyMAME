// license:BSD-3-Clause
// copyright-holders:intealls
/***************************************************************************

    rate_filter.h

    Rate filter for A/V synchronization.

*******************************************************************c********/

#ifndef SRC_OSD_MODULES_SOUND_RATE_FILTER_H_
#define SRC_OSD_MODULES_SOUND_RATE_FILTER_H_

#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <limits>

#include "osdcore.h"

// This is used to determine when to set a new rate. It calculates the
// the variance of a single variable and uses a 95% T-distributed
// confidence interval.

struct online_variance
{
	double mean;
	double M2;
	double variance;
	double sample_variance;
	size_t n;

#define T t_95

#if 0
	double t_995[128] = { 63.65674116,  9.9248432,   5.84090931,  4.60409487,  4.03214298,
	                       3.70742802,  3.4994833 ,  3.35538733,  3.24983554,  3.16927267,
	                       3.10580652,  3.05453959,  3.01227584,  2.97684273,  2.94671288,
	                       2.92078162,  2.89823052,  2.87844047,  2.86093461,  2.84533971,
	                       2.83135956,  2.81875606,  2.80733568,  2.7969395 ,  2.78743581,
	                       2.77871453,  2.77068296,  2.76326246,  2.7563859 ,  2.74999565,
	                       2.74404192,  2.73848148,  2.73327664,  2.72839437,  2.72380559,
	                       2.71948463,  2.71540872,  2.7115576 ,  2.70791318,  2.70445927,
	                       2.7011813 ,  2.69806619,  2.69510208,  2.69227827,  2.68958502,
	                       2.68701349,  2.68455562,  2.68220403,  2.67995197,  2.67779327,
	                       2.67572223,  2.67373363,  2.67182264,  2.6699848 ,  2.66821599,
	                       2.6665124 ,  2.66487048,  2.66328695,  2.66175875,  2.66028303,
	                       2.65885713,  2.65747856,  2.65614503,  2.65485434,  2.65360447,
	                       2.65239352,  2.65121969,  2.6500813 ,  2.64897677,  2.64790462,
	                       2.64686344,  2.64585191,  2.64486878,  2.64391287,  2.64298307,
	                       2.64207831,  2.64119761,  2.64034002,  2.63950463,  2.6386906 ,
	                       2.63789711,  2.63712341,  2.63636876,  2.63563246,  2.63491385,
	                       2.63421231,  2.63352723,  2.63285804,  2.63220419,  2.63156517,
	                       2.63094046,  2.63032961,  2.62973215,  2.62914764,  2.62857567,
	                       2.62801584,  2.62746777,  2.6269311 ,  2.62640546,  2.62589052,
	                       2.62538596,  2.62489148,  2.62440676,  2.62393152,  2.6234655 ,
	                       2.62300841,  2.62256001,  2.62212006,  2.62168831,  2.62126454,
	                       2.62084853,  2.62044007,  2.62003896,  2.61964499,  2.61925798,
	                       2.61887775,  2.61850412,  2.61813691,  2.61777598,  2.61742115,
	                       2.61707227,  2.61672919,  2.61639178,  2.61605988,  2.61573338,
	                       2.61541213,  2.61509601,  2.6147849 };

	double t_98[128] = { 15.89454484,  4.84873221,  3.48190876,  2.99852787,  2.75650852,
	                      2.61224185,  2.51675242,  2.44898499,  2.39844098,  2.35931462,
	                      2.32813983,  2.30272168,  2.28160356,  2.26378128,  2.24854029,
	                      2.23535843,  2.22384531,  2.21370325,  2.20470135,  2.19665775,
	                      2.18942727,  2.18289265,  2.17695811,  2.17154468,  2.16658663,
	                      2.16202887,  2.15782482,  2.15393487,  2.15032509,  2.14696628,
	                      2.14383316,  2.14090372,  2.13815875,  2.13558134,  2.13315663,
	                      2.13087142,  2.12871403,  2.12667402,  2.12474207,  2.12290982,
	                      2.12116974,  2.11951506,  2.11793963,  2.11643791,  2.11500483,
	                      2.11363579,  2.1123266 ,  2.11107341,  2.10987272,  2.10872128,
	                      2.10761613,  2.10655454,  2.10553397,  2.1045521 ,  2.10360677,
	                      2.10269597,  2.10181785,  2.10097067,  2.10015284,  2.09936284,
	                      2.0985993 ,  2.09786089,  2.0971464 ,  2.09645469,  2.09578467,
	                      2.09513536,  2.0945058 ,  2.0938951 ,  2.09330243,  2.09272701,
	                      2.09216808,  2.09162496,  2.09109698,  2.09058351,  2.09008397,
	                      2.0895978 ,  2.08912447,  2.08866347,  2.08821433,  2.0877766 ,
	                      2.08734985,  2.08693366,  2.08652767,  2.08613148,  2.08574476,
	                      2.08536716,  2.08499838,  2.08463809,  2.08428602,  2.08394188,
	                      2.08360542,  2.08327637,  2.08295449,  2.08263955,  2.08233134,
	                      2.08202963,  2.08173422,  2.08144492,  2.08116154,  2.0808839 ,
	                      2.08061183,  2.08034516,  2.08008373,  2.07982739,  2.07957599,
	                      2.0793294 ,  2.07908746,  2.07885007,  2.07861707,  2.07838837,
	                      2.07816383,  2.07794334,  2.07772681,  2.07751411,  2.07730516,
	                      2.07709985,  2.07689808,  2.07669977,  2.07650483,  2.07631318,
	                      2.07612472,  2.07593939,  2.0757571 ,  2.07557778,  2.07540137,
	                      2.07522777,  2.07505695,  2.07488881 };
#endif

	double t_95[128] = {  6.31375151, 2.91998558, 2.35336343, 2.13184679, 2.01504837,
	                      1.94318028, 1.89457861, 1.85954804, 1.83311293, 1.81246112,
	                      1.79588482, 1.78228756, 1.7709334 , 1.76131014, 1.75305036,
	                      1.74588368, 1.73960673, 1.73406361, 1.72913281, 1.72471824,
	                      1.7207429 , 1.71714437, 1.71387153, 1.71088208, 1.70814076,
	                      1.70561792, 1.70328845, 1.70113093, 1.69912703, 1.69726089,
	                      1.69551878, 1.69388875, 1.69236031, 1.69092426, 1.68957246,
	                      1.68829771, 1.68709362, 1.68595446, 1.68487512, 1.68385101,
	                      1.682878  , 1.68195236, 1.6810707 , 1.68022998, 1.67942739,
	                      1.67866041, 1.67792672, 1.6772242 , 1.67655089, 1.67590503,
	                      1.67528495, 1.67468915, 1.67411624, 1.67356491, 1.67303397,
	                      1.6725223 , 1.67202889, 1.67155276, 1.67109303, 1.67064886,
	                      1.67021948, 1.66980416, 1.66940222, 1.66901303, 1.66863598,
	                      1.66827051, 1.66791611, 1.66757228, 1.66723855, 1.66691448,
	                      1.66659966, 1.6662937 , 1.66599622, 1.66570689, 1.66542537,
	                      1.66515135, 1.66488454, 1.66462464, 1.66437141, 1.66412458,
	                      1.66388391, 1.66364918, 1.66342017, 1.66319668, 1.6629785 ,
	                      1.66276545, 1.66255735, 1.66235403, 1.66215533, 1.66196108,
	                      1.66177116, 1.6615854 , 1.66140367, 1.66122586, 1.66105182,
	                      1.66088144, 1.66071461, 1.66055122, 1.66039116, 1.66023433,
	                      1.66008063, 1.65992998, 1.65978227, 1.65963744, 1.65949538,
	                      1.65935603, 1.65921931, 1.65908514, 1.65895346, 1.65882419,
	                      1.65869727, 1.65857263, 1.65845022, 1.65832997, 1.65821183,
	                      1.65809574, 1.65798166, 1.65786952, 1.65775928, 1.6576509 ,
	                      1.65754432, 1.6574395 , 1.6573364 , 1.65723497, 1.65713518,
	                      1.65703698, 1.65694034, 1.65684523 };

	online_variance()
	{
		reset();
	}
	void reset()
	{
		mean = 0;
		M2 = 0;
		variance = std::numeric_limits<double>::infinity();
		sample_variance = std::numeric_limits<double>::infinity();
		n = 0;
	}
	void update(double x)
	{
		n += 1;
		double delta = x - mean;
		mean += delta / n;
		double delta2 = x - mean;
		M2 += delta * delta2;

		if (n > 2)
		{
			variance = M2 / n;
			sample_variance = M2 / (n - 1);
		}
	}
	double interval()
	{
		if (n < 2)
			return std::numeric_limits<double>::infinity();

		return T[(n >= 127) ? 127 : n] * sqrtf(sample_variance);
	}
};

// Linear regression with forgetting factor, allows for slight rate adaptation.
// There are two ways of dealing with outliers, if we detect a significant
// step in audio in vs out, we restart the estimation. These are usually caused
// by framedrops. Smaller outliers (audio card problems mostly) can sneak in
// but will not cause a static error, since we regress with a forgetting factor.

struct exp_fit
{
	double m_meanX;
	double m_meanY;
	double m_varX;
	double m_covXY;
	double m_n;
	double m_meanXY;
	double m_varY;
	double m_alpha;

	double m_x0;
	double m_y0;

	double lim;
	double slope_lim;

	double m_rate_min;
	double m_rate_max;

	online_variance var;

	exp_fit(double alpha, double limit, double rate_min, double rate_max)
	{
		m_alpha = alpha;
		reset(0.0, 0.0);

		lim = limit;
		slope_lim = 1.0;

		m_rate_min = rate_min;
		m_rate_max = rate_max;
	}

	void reset(double x, double y)
	{
		m_x0 = x;
		m_y0 = y;

		m_meanX = 0;
		m_meanY = 0;
		m_varX = 0;
		m_covXY = 0;
		m_n = 0;
		m_meanXY = 0;
		m_varY = 0;

		var.reset();
	}

	double linreg_max(double a, double b)
	{
		if (a > b)
			return a;
		return b;
	}

	void update(double x, double y)
	{
		double xt = x - m_x0;
		double yt = y - m_y0;

		m_n += 1;

		double alpha = linreg_max(m_alpha, 1.0 / m_n);

		double dx = xt - m_meanX;
		double dy = yt - m_meanY;
		double dxy = (xt * yt) - m_meanXY;

		m_varX += ((1 - alpha) * dx * dx - m_varX) * alpha;
		m_varY += ((1 - alpha) * dy * dy - m_varY) * alpha;
		m_covXY += ((1 - alpha) * dx * dy - m_covXY) * alpha;

		m_meanX += dx * alpha;
		m_meanY += dy * alpha;
		m_meanXY += dxy  * alpha;

		double st = slope();

		if (st > m_rate_min && st < m_rate_max && m_n > 2)
			var.update(st);

		if (var.interval() < lim && m_n > 15)
			slope_lim = st;
	}

	double slope()
	{
		return m_covXY / m_varX;
	}
	double slope_out()
	{
		return slope_lim;
	}
	double confidence_interval()
	{
		return var.interval();
	}
};

// Used to detect significant steps in audio streaming. Mainly used to
// detect framedrops.

struct step_detector
{
	double* m_x;
	double* m_y;

	size_t m_nmax;
	size_t m_n;
	size_t m_holdoff;

	int m_sample_rate;

	double m_d;
	double m_thres;
	int m_over_thres_ct;
	double m_d_offset_alpha;
	double m_d_offset_max;
	double m_d_offset;

	step_detector(size_t nmax, int sample_rate)
	{
		m_nmax = nmax;
		m_x = new double[nmax]();
		m_y = new double[nmax]();
		m_n = 0;
		m_holdoff = nmax;
		m_d = 0;
		m_sample_rate = sample_rate;
		m_over_thres_ct = 0;
		m_d_offset_alpha = 0.95;
		m_d_offset_max = 0.008 * sample_rate; // maximum 8 ms
		m_d_offset = m_d_offset_max;
		m_thres = m_d_offset_max;
	}
	bool update(double x, double y, int callback_count)
	{
		double f = 0;
		double s = 0;
		online_variance thres_var;

		if (m_holdoff != 0)
			m_holdoff--;

		m_x[m_n] = x;
		m_y[m_n] = y;
		m_n = (m_n + 1) % m_nmax;

		for (size_t i = 0; i < m_nmax / 2; i++) {
			double fv = m_x[(i + m_n) % m_nmax] - m_y[(i + m_n) % m_nmax];
			double sv = m_x[(i + m_n + m_nmax / 2) % m_nmax] - m_y[(i + m_n + m_nmax / 2) % m_nmax];
			f += fv;
			s += sv;
			thres_var.update(fv);
		}

		f /= m_nmax / 2;
		s /= m_nmax / 2;

		m_d = fabs(f - s);

		m_thres = std::min(thres_var.interval() + m_d_offset, m_d_offset_max);

		if (m_d > m_thres)
			m_over_thres_ct++;
		else
			m_over_thres_ct = 0;

		// if the game is not running at 100% speed, we'll have a slight constant offset, so estimate this and take into account.
		m_d_offset = (m_d_offset * m_d_offset_alpha) + (m_d * (1 - m_d_offset_alpha));
		m_d_offset = std::min(m_d_offset, m_d_offset_max);

		if (m_over_thres_ct >= m_nmax / 4) {
			if (m_holdoff == 0)
				osd_printf_verbose("rate filter restart, diff: %.3f ms, thres: %.3f ms, callback_count: %lu\n",
				                   m_d / m_sample_rate * 1e3,
				                   m_thres / m_sample_rate * 1e3,
				                   callback_count);
			m_holdoff = m_nmax;
			m_over_thres_ct = 0;
			m_d_offset = m_d_offset_max;
		}

		return m_holdoff == 0;
	}
	double mid_x()
	{
		return m_x[(m_n + m_nmax / 2) % m_nmax];
	}
	double mid_y()
	{
		return m_y[(m_n + m_nmax / 2) % m_nmax];
	}
};

struct rate_filter
{
	exp_fit* m_final_rate;
	step_detector* m_step;
	bool m_stepret;

	int m_sample_rate;
	int m_max_callback_count;
	bool m_warning_shown;
	size_t m_n;

	rate_filter(size_t n,
	            int sample_rate,
	            double alpha,
	            double final_rate_pm,
	            double final_rate_min,
	            double final_rate_max)
	{
		m_n = n;
		m_sample_rate = sample_rate;
		m_stepret = false;
		m_max_callback_count = 0;
		m_warning_shown = false;

		m_final_rate = new exp_fit(alpha,
		                           final_rate_pm,
		                           final_rate_min,
		                           final_rate_max);

		m_step = new step_detector(n, sample_rate);
	}
	~rate_filter()
	{
		delete m_final_rate;
	}
	void update(u64 x, u64 y, int callback_count)
	{
		m_max_callback_count = (callback_count > m_max_callback_count) ? callback_count : m_max_callback_count;

		m_stepret = m_step->update(x, y, callback_count);

		// We print a warning when getting the rate if latency is too high.
		// This is convoluted but has a reason (emuopts accessibility).
		if (callback_count > m_sample_rate / 60 / 2 && !m_warning_shown)
		{
			osd_printf_warning("WARNING: Audio buffer rate estimation works best with latencies < 16 ms. "
			                   "Callback count indicates %.1f ms. Try decreasing pa_latency.\n",
			                   2e3 * (double) m_max_callback_count / m_sample_rate);
			m_warning_shown = true;
		}

		if (m_stepret)
		{
			// If we get an outlier, it's not going to be in the middle
			m_final_rate->update(m_step->mid_x() / m_sample_rate, m_step->mid_y() / m_sample_rate);
		}
		else
		{
			m_final_rate->reset(m_step->mid_x() / m_sample_rate, m_step->mid_y() / m_sample_rate);
		}
	}
	double filtered_rate()
	{
		return m_final_rate->slope_out();
	}
};

#endif
