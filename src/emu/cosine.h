// license:BSD-3-Clause
// copyright-holders:intealls
/***************************************************************************

    cosine.h

    Cosine resampling for final audio mix

***************************************************************************/

#ifndef SRC_EMU_COSINE_H_
#define SRC_EMU_COSINE_H_

class cosine_resampler
{
public:
	cosine_resampler() : m_remainder(0.0)
	{
		for (int i = 0; i < n; i++)
			table[i] = (1.0f - cosf((float)i / n * M_PI)) * 0.5f;
	};

	void apply(double rate, const int16_t* input, size_t input_frames, int16_t* output, size_t* output_frames, int num_channels)
	{
		*output_frames = ((float)input_frames) * rate + m_remainder;
		m_remainder -= (int)m_remainder;

		resample(input, input_frames, output, *output_frames, num_channels);
		m_remainder += ((float)input_frames) * rate - *output_frames;
	}
private:
	static const int n = 10000;
	float table[n];
	double m_remainder;

	void resample(const int16_t* input, size_t input_frames, int16_t* output, size_t output_frames, int num_channels)
	{
		if (!input || !output || input_frames == 0 || output_frames == 0 || num_channels <= 0)
			return;

		float resample_factor = (float)output_frames / input_frames;

		if (resample_factor <= 0.0f)
			return;

		float input_pos = 0.0f;

		for (size_t out_idx = 0; out_idx < output_frames; ++out_idx) {
			size_t in_idx = floorf(input_pos);
			float frac = input_pos - in_idx;  // fractional part

			// Clamp to avoid reading past the last sample
			size_t in_idx_next =
					(in_idx + 1 < input_frames) ? (in_idx + 1) : (input_frames - 1);

			float mu = table[static_cast<int>(std::clamp<float>(frac, 0, 1) * (n - 1))];

			for (int ch = 0; ch < num_channels; ++ch) {
				float y1 = input[in_idx * num_channels + ch];
				float y2 = input[in_idx_next * num_channels + ch];
				float interpolated = y1 + (y2 - y1) * mu;

				if (interpolated > 32767.0f)
					interpolated = 32767.0f;
				else if (interpolated < -32768.0f)
					interpolated = -32768.0f;

				output[out_idx * num_channels + ch] = interpolated;
			}

			input_pos += 1.0f / resample_factor;
		}
	}
};

#endif /* SRC_EMU_COSINE_H_ */
