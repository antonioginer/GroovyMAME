// license:BSD-3-Clause
// copyright-holders:O. Galibert, intealls


#include "sound_module.h"

#include <algorithm>
#include <cassert>
#include <utility>

sound_module::~sound_module()
{
	// implementing this here forces the vtable and inline virtual member functions to be instantiated
}

sound_module::abuffer::abuffer(uint32_t channels) noexcept : m_channels(channels), m_used_buffers(0), m_unused_buffers(0), m_last_sample(channels, 0)
{
	m_buf_maintenance = osd_ticks();
}

void sound_module::abuffer::get(int16_t *data, uint32_t samples) noexcept
{
	osd_ticks_t ticks_now = osd_ticks();

	uint32_t pos = 0;
	while(pos != samples) {
		if(!m_used_buffers) {
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

	// this tracks the number of unused buffers (which can be dropped safely)
	m_unused_buffers = std::min(m_used_buffers, m_unused_buffers);

	if (ticks_now - m_buf_maintenance > 2 * osd_ticks_per_second()) {
		m_unused_buffers = m_used_buffers;
		m_buf_maintenance = ticks_now;
	}
}

void sound_module::abuffer::push(const int16_t *data, uint32_t samples)
{
	auto &buf = push_buffer();

	const int buf_safety_margin = 2;
	const int buf_keep = std::clamp(m_used_buffers - m_unused_buffers + buf_safety_margin, 0, 5);

	buf.m_cpos = 0;
	buf.m_data.resize(samples * m_channels);
	std::copy_n(data, samples * m_channels, buf.m_data.data());
	std::copy_n(data + ((samples - 1) * m_channels), m_channels, m_last_sample.data());

	if(m_used_buffers > buf_keep) {
		// If there are too many buffers, drop so we only keep the number we need (reduces latency)
		for(unsigned i = 0; i < buf_keep; i++) {
			using std::swap;
			swap(m_buffers[i], m_buffers[m_used_buffers + i - buf_keep]);
		}
		m_used_buffers = buf_keep;
	}
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
