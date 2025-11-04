#include <cmath>

struct kalman_filter
{
	// State: t (timestamp) and P (period)
	double t = 0.0;
	double P = 0.0;

	// Covariance matrix (2x2)
	double C11 = 1e6, C12 = 0.0;
	double C21 = 0.0, C22 = 1e6;

	// Process noise
	double Q_t = 1e-3;  // model noise for timestamp (position)
	double Q_P = 1e-6;  // model noise for period (velocity)

	// Measurement noise
	double R = 1e2;     // variance of measurement noise

	bool initialized = false;

	// Update the filter with a new timestamp measurement
	void update(double z)
	{
		if (!initialized)
		{
			t = z;
			P = 0.0; // initial period unknown
			initialized = true;
			return;
		}

		// --- Prediction step ---
		double t_pred = t + P;
		double P_pred = P;

		// Covariance propagation: F = [[1,1],[0,1]]
		double C11p = C11 + 2 * C12 + C22 + Q_t;
		double C12p = C12 + C22;
		double C21p = C21 + C22;
		double C22p = C22 + Q_P;

		// --- Update step ---
		double y = z - t_pred;          // innovation (measurement residual)
		double S = C11p + R;            // innovation covariance
		double K1 = C11p / S;           // Kalman gain for t
		double K2 = C21p / S;           // Kalman gain for P

		// Corrección del estado
		t = t_pred + K1 * y;
		P = P_pred + K2 * y;

		// Update the covariance (Joseph-form simplified)
		double C11n = (1 - K1) * C11p;
		double C12n = (1 - K1) * C12p;
		double C21n = C21p - K2 * C11p;
		double C22n = C22p - K2 * C12p;

		C11 = C11n;
		C12 = C12n;
		C21 = C21n;
		C22 = C22n;
	}

	// Get the current estimated period
	double get_period() const { return P; }

	// Get the current filtered timestamp
	uint64_t get_filtered_timestamp() const { return (uint64_t)t; }

	// Reset filter
	void reset()
	{
		t = 0.0;
		P = 0.0;
		C11 = 1e6, C12 = 0.0;
		C21 = 0.0, C22 = 1e6;
		initialized = false;
	}
};
