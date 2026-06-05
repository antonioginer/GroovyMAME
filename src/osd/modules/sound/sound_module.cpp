// license:BSD-3-Clause
// copyright-holders:O. Galibert, intealls

#include "sound_module.h"


sound_module::~sound_module()
{
	// implementing this here forces the vtable and inline virtual member functions to be instantiated
}

sound_module::abuffer::abuffer(uint32_t channels, int rate) noexcept :
	m_rate(rate),
	m_channels(channels),
	m_buffer_min_ct(1e8),
	m_skip_threshold((20.f / 1000.f) * rate + 0.5f),
	m_osd_ticks(osd_ticks()),
	m_skip_threshold_ticks(m_osd_ticks),
	m_xfade_length(rate * xfade_length / 1000),
	m_xfade_buf(m_xfade_length * m_channels),
	m_xfade_total(0),
	m_xfade_remaining(0)
{
	m_ab = std::make_unique<buffer<int16_t>>(rate, channels);
}

uint32_t sound_module::abuffer::available()
{
	int count = m_ab->count() / m_channels;

	if (count <= 0)
		return 0;

	// reserve last sample for underruns, will sustain last sample to reduce crackles
	return (count - 1) * m_channels;
}

void sound_module::abuffer::clear()
{
	// only used from xaudio2 consumer
	m_ab->increment_playpos(m_ab->count());
	m_xfade_total = m_xfade_remaining = 0;
	m_buffer_min_ct = 1e8;
	m_skip_threshold_ticks = osd_ticks();
}

void sound_module::abuffer::set_latency(float latency)
{
	if (latency == 0.f)
		latency = 20.f;

	latency = std::clamp<float>(latency, 1.f, 100.f);
	m_skip_threshold = (latency / 1000.f) * m_rate + 0.5f;
}

void sound_module::abuffer::get(int16_t *data, uint32_t samples) noexcept
{
	int buf_ct = available() / m_channels;

	if (buf_ct >= samples) {
		m_ab->read(data, samples * m_channels);

		if (m_xfade_remaining > 0) {
			int offset = m_xfade_total - m_xfade_remaining;
			int blend_frames = std::min<int>(m_xfade_remaining, samples);

			for (int i = 0; i < blend_frames; i++) {
				float t = static_cast<float>(offset + i) / static_cast<float>(std::max<int>(1, m_xfade_total));

				for (uint32_t ch = 0; ch < m_channels; ch++) {
					int idx = i * m_channels + ch;
					int xf_idx = (offset + i) * m_channels + ch;
					float old_sample = m_xfade_buf[xf_idx];
					float new_sample = data[idx];
					float blend = old_sample * (1.0f - t) + new_sample * t;
					data[idx] = static_cast<int16_t>(std::clamp(blend, -32768.0f, 32767.0f));
				}
			}

			m_xfade_remaining -= blend_frames;
		}

		// keep track of the minimum buffer count, skip samples adaptively to respect the audio_latency setting
		buf_ct -= samples;

		if (buf_ct < m_buffer_min_ct)
			m_buffer_min_ct = buf_ct;

		// if we are below the threshold, reset the counter
		if (buf_ct < m_skip_threshold)
			m_skip_threshold_ticks = m_osd_ticks;

		// if we have been above the set threshold for ~0.5 seconds, skip forward
		if (m_osd_ticks - m_skip_threshold_ticks > osd_ticks_per_second() / 2) {
			int adjust = m_buffer_min_ct - m_skip_threshold;

			// if adjustment is less than one millisecond, don't bother
			if (adjust > m_rate / 1000) {
				int peeked = m_ab->peek(m_xfade_buf.data(), m_xfade_length * m_channels);
				m_ab->increment_playpos(adjust * m_channels);
				m_xfade_total = peeked / m_channels;
				m_xfade_remaining = m_xfade_total;
			}

			m_skip_threshold_ticks = m_osd_ticks;
			m_buffer_min_ct = 1e8;
		}
	} else {
		m_ab->read(data, buf_ct * m_channels);
		data += buf_ct * m_channels;

		// sustain last sample instead of just clipping to 0, helps out with crackles
		for (int i = 0; i < samples - buf_ct; i++) {
			int16_t* dst = data + i * m_channels;
			if (!m_ab->peek(dst, m_channels)) {
				// if completely empty zero fill (will rarely happen)
				std::fill(dst, dst + (samples - buf_ct - i) * m_channels, 0);
				break;
			}
		}

		m_skip_threshold_ticks = m_osd_ticks;
	}
}

void sound_module::abuffer::push(const int16_t *data, uint32_t samples)
{
	// for determining buffer overflows, take the sample here instead of in the callback
	m_osd_ticks = osd_ticks();

	m_ab->write(data, samples * m_channels);
}
