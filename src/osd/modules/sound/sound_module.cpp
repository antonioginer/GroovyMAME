// license:BSD-3-Clause
// copyright-holders:O. Galibert

#include "sound_module.h"
#include "rate_filter.h"

#include <algorithm>
#include <cassert>
#include <utility>

sound_module::~sound_module()
{
	// implementing this here forces the vtable and inline virtual member functions to be instantiated
}

#if !USE_AB
sound_module::abuffer::abuffer(uint32_t channels) noexcept : m_channels(channels), m_used_buffers(0), m_last_sample(channels, 0)
{
	m_delta = 0;
	m_delta2 = 0;
}

void sound_module::abuffer::get(int16_t *data, uint32_t samples) noexcept
{
	m_delta -= samples;
	m_delta2 -= samples;
	uint32_t pos = 0;
	while(pos != samples) {
		if(!m_used_buffers) {
			m_delta2 += samples - pos;
			while(pos != samples) {
				std::copy_n(m_last_sample.data(), m_channels, data);
				data += m_channels;
				pos++;
			}
			break;
		}

		auto &buf = m_buffers.front();
		if(buf.m_data.empty()) {
			pop_buffer();
			continue;
		}

		uint32_t avail = (buf.m_data.size() / m_channels) - buf.m_cpos;
		if(avail > (samples - pos)) {
			avail = samples - pos;
			std::copy_n(buf.m_data.data() + (buf.m_cpos * m_channels), avail * m_channels, data);
			buf.m_cpos += avail;
			break;
		}

		std::copy_n(buf.m_data.data() + (buf.m_cpos * m_channels), avail * m_channels, data);
		pop_buffer();
		pos += avail;
		data += avail * m_channels;
	}
	//  printf("# %d %d\n", m_delta, m_delta2);
}

void sound_module::abuffer::push(const int16_t *data, uint32_t samples)
{
	m_delta += samples;
	m_delta2 += samples;
	auto &buf = push_buffer();
	buf.m_cpos = 0;
	buf.m_data.resize(samples * m_channels);
	std::copy_n(data, samples * m_channels, buf.m_data.data());
	std::copy_n(data + ((samples - 1) * m_channels), m_channels, m_last_sample.data());

	if(m_used_buffers > 10) {
		for(uint32_t i=0; i != m_used_buffers-10; i++)
			m_delta2 -= (m_buffers[i].m_data.size()/m_channels - m_buffers[i].m_cpos);
		// If there are way too many buffers, drop some so only 10 are left (roughly 0.2s)
		for(unsigned i = 0; 10 > i; ++i) {
			using std::swap;
			swap(m_buffers[i], m_buffers[m_used_buffers + i - 10]);
		}
		m_used_buffers = 10;
	} else if(m_used_buffers >= 5) {
		// If there are too many buffers, remove five samples per buffer
		// to slowly resync to reduce latency (4 seconds to
		// compensate one buffer, roughly)
		m_delta2 -= std::max<uint32_t>(samples / 200, 1);
		buf.m_cpos = std::max<uint32_t>(samples / 200, 1);
	}
	//  printf("# %d %d\n", m_delta, m_delta2);
}

uint32_t sound_module::abuffer::available() const noexcept
{
	uint32_t result = 0;
	for(uint32_t i = 0; m_used_buffers > i; ++i)
		result += (m_buffers[i].m_data.size() / m_channels) - m_buffers[i].m_cpos;
	return result;
}

inline void sound_module::abuffer::pop_buffer() noexcept
{
	assert(m_used_buffers);
	if(--m_used_buffers) {
		auto temp(std::move(m_buffers.front()));
		for(uint32_t i = 0; m_used_buffers > i; ++i)
			m_buffers[i] = std::move(m_buffers[i + 1]);
		m_buffers[m_used_buffers] = std::move(temp);
	}
}

inline sound_module::abuffer::buffer &sound_module::abuffer::push_buffer()
{
	if(m_buffers.size() > m_used_buffers) {
		return m_buffers[m_used_buffers++];
	} else {
		assert(m_buffers.size() == m_used_buffers);
		++m_used_buffers;
		return m_buffers.emplace_back();
	}
}
#else

sound_module::abuffer::abuffer(uint32_t channels) noexcept : m_channels(channels), m_used_buffers(0), m_last_sample(channels, 0)
{
}

sound_module::abuffer::abuffer(uint32_t channels, int rate, float audio_latency) noexcept : m_channels(channels), m_used_buffers(0), m_last_sample(channels, 0)
{
	ab = new audio_buffer<int16_t>(rate * channels, channels);
	callback_ct = 0;
	samples_in = 0;
	samples_out = 0;
	buffer_min_ct = 0;
	skip_threshold = ((1.5 + audio_latency * 3.0) / 1000.0) * rate * 2 + 0.5f;
	underflow = false;
	overflow = false;
	skip_threshold_ticks = 0;
	m_osd_ticks = 0;
}

void sound_module::abuffer::get(int16_t *data, uint32_t samples) noexcept
{
	int buf_ct = ab->count() / m_channels;

	// osd_printf_verbose("pull %d\n", samples);

	if (buf_ct >= samples)
	{
		ab->read(data, samples * m_channels);

		// keep track of the minimum buffer count, skip samples adaptively to respect the audio_latency setting
		buf_ct -= samples;

		if (buf_ct < buffer_min_ct)
			buffer_min_ct = buf_ct;

		// if we are below the threshold, reset the counter
		if (buf_ct < skip_threshold)
			skip_threshold_ticks = m_osd_ticks;

		// if we have been above the set threshold for ~1 second, skip forward
		if (m_osd_ticks - skip_threshold_ticks > osd_ticks_per_second())
		{
			int adjust = buffer_min_ct - skip_threshold / 2;

			// if adjustment is less than two milliseconds, don't bother
			if (adjust > 48000 / 500) {
				ab->increment_playpos(adjust * m_channels);
				overflow = true;
			}

			skip_threshold_ticks = m_osd_ticks;
			buffer_min_ct = 1e8;
		}
	}
	else
	{
		ab->read(data, buf_ct * m_channels);
		std::memset(data + (buf_ct * m_channels), 0, (samples - buf_ct) * m_channels);

		// if update_audio_stream has been called, note the underflow
		if (m_osd_ticks)
			underflow = true;

		skip_threshold_ticks = m_osd_ticks;
	}

	samples_out += samples;
	callback_ct = samples;
}

double lolrate = 1.0;
extern u64 sm_samples_in;

void sound_module::abuffer::push(const int16_t *data, uint32_t samples)
{
	static struct rate_filter rf(30,
	                             48000,
	                             0.002,
	                             0.003,
	                             0.98,
	                             1.02);

	samples_in = sm_samples_in;

#if 0 && DEBUG_AB
	osd_printf_verbose("count: %f, callback_ct: %d\n", ab->count() / m_channels, callback_ct);
	osd_printf_verbose("m_channels: %d\n", m_channels);
#endif

	if (overflow)
	{
		osd_printf_verbose("overflow\n");
		overflow = false;
	}

	if (underflow)
	{
		osd_printf_verbose("underflow\n");
		// add some silence to prevent immediate underflows
		ab->clear(skip_threshold * m_channels / 2);
		underflow = false;
	}

	ab->write(data, samples * m_channels);

	// for determining buffer overflows, take the sample here instead of in the callback
	m_osd_ticks = osd_ticks();

	rf.update(samples_out, samples_in, callback_ct);

	lolrate = rf.filtered_rate();

#if DEBUG_AB
	osd_printf_verbose("rate: %f, count: %d\n", rf.filtered_rate(), ab->count() / m_channels);
#endif
}

uint32_t sound_module::abuffer::available() const noexcept
{
	return ab->count();
}

inline void sound_module::abuffer::pop_buffer() noexcept
{
}

inline sound_module::abuffer::buffer &sound_module::abuffer::push_buffer()
{
	++m_used_buffers;
	return m_buffers.emplace_back();
}

#endif
