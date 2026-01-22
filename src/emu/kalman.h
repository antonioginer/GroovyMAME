// license:BSD-3-Clause
// copyright-holders:intealls
//============================================================
//
//  kalman.h
//
//  Kalman filter, used to denoise vblank timestamps.
//
//============================================================

#include <cmath>

struct kalman_filter
{
	double x[2];       // state vector (2x1)
	double P[2][2];    // covariance (2x2)

	double F[2][2];    // state transition (2x2)
	double Q[2][2];    // process noise (2x2)
	double H[1][2];    // measurement matrix (1x2)
	double R;          // initial measurement noise (scalar)
	double incR;       // measurement noise increment (scalar)
	double maxR;       // max measurement noise (scalar)

	bool initialized = false;

	// Multiply 2x2 * 2x1 = 2x1
	void mat2x2_mul_2x1(double A[2][2], double v[2], double out[2])
	{
		out[0] = A[0][0] * v[0] + A[0][1] * v[1];
		out[1] = A[1][0] * v[0] + A[1][1] * v[1];
	}

	// Multiply 2x2 * 2x2 = 2x2
	void mat2x2_mul(double A[2][2], double B[2][2], double out[2][2])
	{
		out[0][0] = A[0][0] * B[0][0] + A[0][1] * B[1][0];
		out[0][1] = A[0][0] * B[0][1] + A[0][1] * B[1][1];
		out[1][0] = A[1][0] * B[0][0] + A[1][1] * B[1][0];
		out[1][1] = A[1][0] * B[0][1] + A[1][1] * B[1][1];
	}

	// Transpose 1x2 → 2x1
	void mat1x2_T(double v[1][2], double out[2])
	{
		out[0] = v[0][0];
		out[1] = v[0][1];
	}

	// Multiply 1x2 * 2x2 = 1x2
	void mat1x2_mul_2x2(double A[1][2], double B[2][2], double out[1][2])
	{
		out[0][0] = A[0][0] * B[0][0] + A[0][1] * B[1][0];
		out[0][1] = A[0][0] * B[0][1] + A[0][1] * B[1][1];
	}

	// Multiply 1x2 * 2x1 = scalar
	double mat1x2_mul_2x1(double A[1][2], double v[2])
	{
		return A[0][0] * v[0] + A[0][1] * v[1];
	}

	void reset()
	{
		initialized = false;
	}

	void init(double time, double period)
	{
		x[0] = time;
		x[1] = period;

		// F matrix (constant period and dt)
		F[0][0] = 1;
		F[0][1] = 1;
		F[1][0] = 0;
		F[1][1] = 1;

		// Measurement matrix (we measure time only)
		H[0][0] = 1;
		H[0][1] = 0;

		// Covariance initialized large
		P[0][0] = 100;
		P[0][1] = 0;
		P[1][0] = 0;
		P[1][1] = 1;

		// Process noise (filterpy defaults)
		Q[0][0] = 2.5e-7;
		Q[0][1] = 5.0e-7;
		Q[1][0] = 5.0e-7;
		Q[1][1] = 1.0e-6;

		// Measurement noise (start out fast and slow down)
		R = 1e-4;
		incR = 1.05;
		maxR = 1e2;

		initialized = true;
	}

	void predict()
	{
		// x = F x
		double Fx[2];
		mat2x2_mul_2x1(F, x, Fx);

		x[0] = Fx[0];
		x[1] = Fx[1];

		// P = F P F^T + Q
		double FP[2][2];
		mat2x2_mul(F, P, FP);

		double F_T[2][2] = { { F[0][0], F[1][0] },
		                     { F[0][1], F[1][1] } };

		double FPFt[2][2];
		mat2x2_mul(FP, F_T, FPFt);

		P[0][0] = FPFt[0][0] + Q[0][0];
		P[0][1] = FPFt[0][1] + Q[0][1];
		P[1][0] = FPFt[1][0] + Q[1][0];
		P[1][1] = FPFt[1][1] + Q[1][1];
	}

	void update(double z)
	{
		// y = z - Hx
		double Hx = mat1x2_mul_2x1(H, x);
		double y = z - Hx;

		// S = H P H^T + R
		double HP[1][2];
		mat1x2_mul_2x2(H, P, HP);

		double H_T[2];
		mat1x2_T(H, H_T);

		double S = HP[0][0] * H_T[0] + HP[0][1] * H_T[1] + R;

		// K = P H^T / S
		double K[2];
		K[0] = (P[0][0] * H_T[0] + P[0][1] * H_T[1]) / S;
		K[1] = (P[1][0] * H_T[0] + P[1][1] * H_T[1]) / S;

		// x = x + K y
		x[0] += K[0] * y;
		x[1] += K[1] * y;

		// P = (I - K H) P
		double KH[2][2] = { { K[0] * H[0][0], K[0] * H[0][1] },
		                    { K[1] * H[0][0], K[1] * H[0][1] } };

		double I_KH[2][2] = { { 1 - KH[0][0],   -KH[0][1] },
		                      {    -KH[1][0], 1- KH[1][1] } };

		double newP[2][2];
		mat2x2_mul(I_KH, P, newP);

		P[0][0] = newP[0][0];
		P[0][1] = newP[0][1];
		P[1][0] = newP[1][0];
		P[1][1] = newP[1][1];

		// Start out fast to quickly converge to a usable estimate
		R *= incR;
		R = R > maxR ? maxR : R;
	}

	// Get the current estimated period
	double get_period() const { return x[1]; }

	// Get the current filtered timestamp
	uint64_t get_filtered_timestamp() const { return (uint64_t)x[0]; }
};
