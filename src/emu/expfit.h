// license:BSD-3-Clause
// copyright-holders:intealls
/***************************************************************************

    expfit.h

    Linear regression with forgetting factor, allows for rolling adaptation
    of estimate. Used to estimate actual audio sink frequency.

***************************************************************************/

#ifndef SRC_EMU_EXPFIT_H_
#define SRC_EMU_EXPFIT_H_

#include <cmath>
#include <algorithm>
#include <limits>

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

	int m_lim;
	double m_slope_lim;

	exp_fit(double alpha, double x, double y)
	{
		if (!(alpha > 0.0 && alpha <= 1.0))
			alpha = 0.5;

		m_alpha = alpha;
		reset(x, y);

		// use estimate when 10% of initial value remains
		m_lim = std::log(0.10) / std::log(1.0 - alpha) + 0.5;

		if (m_lim < 1)
			m_lim = 1;
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

		m_slope_lim = 0.0;
	}

	void update(double x, double y)
	{
		if (!std::isfinite(x) || !std::isfinite(y))
			return;

		double xt = x - m_x0;
		double yt = y - m_y0;

		m_n += 1;

		double alpha = std::max<double>(m_alpha, 1.0 / m_n);

		double dx = xt - m_meanX;
		double dy = yt - m_meanY;
		double dxy = (xt * yt) - m_meanXY;

		m_varX += ((1 - alpha) * dx * dx - m_varX) * alpha;
		m_varY += ((1 - alpha) * dy * dy - m_varY) * alpha;
		m_covXY += ((1 - alpha) * dx * dy - m_covXY) * alpha;

		m_meanX += dx * alpha;
		m_meanY += dy * alpha;
		m_meanXY += dxy * alpha;

		if (m_n > m_lim)
			m_slope_lim = slope();
	}

	double slope()
	{
		if (std::abs(m_varX) < std::numeric_limits<double>::epsilon())
			return 0.0;

		double out = m_covXY / m_varX;

		if (!std::isfinite(out))
			return 0.0;

		return out;
	}

	double slope_out()
	{
		return m_slope_lim;
	}
};

#endif /* SRC_EMU_EXPFIT_H_ */
