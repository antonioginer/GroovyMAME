// license:BSD-3-Clause
// copyright-holders:O. Galibert

#ifndef MAME_OSD_SOUND_SOUND_MODULE_H
#define MAME_OSD_SOUND_SOUND_MODULE_H

#pragma once

#include <osdepend.h>
#include <osdcore.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#define OSD_SOUND_PROVIDER   "sound"

class sound_module
{
public:
	virtual ~sound_module();

	virtual uint32_t get_generation() = 0;
	virtual osd::audio_info get_information() = 0;
	virtual bool external_per_channel_volume() { return false; }
	virtual bool split_streams_per_source() { return false; }

	virtual uint32_t stream_sink_open(uint32_t node, std::string name, uint32_t rate) = 0;
	virtual uint32_t stream_source_open(uint32_t node, std::string name, uint32_t rate) { return 0; }
	virtual void stream_set_volumes(uint32_t id, const std::vector<float> &db) {}
	virtual void stream_close(uint32_t id) = 0;
	virtual void stream_sink_update(uint32_t id, const int16_t *buffer, int samples_this_frame) = 0;
	virtual void stream_source_update(uint32_t id, int16_t *buffer, int samples_this_frame) {}

	virtual void begin_update() {}
	virtual void end_update() {}

protected:
	class abuffer
	{
	public:
		abuffer(uint32_t channels, int rate) noexcept;
		void set_latency(float latency);
		void clear();
		void get(int16_t *data, uint32_t samples) noexcept;
		void push(const int16_t *data, uint32_t samples);
		uint32_t channels() const noexcept { return m_channels; }
		uint32_t available();

	private:
		const int xfade_length = 4; // 4 ms

		template<typename T>
		struct buffer {
			int m_reserve;
			int m_capacity;
			std::vector<T> m_buf;
			std::atomic<int> m_playpos{0}, m_writepos{0};

			buffer(int rate, int channels)
				: m_reserve(channels) , m_capacity(rate * channels + m_reserve) , m_buf(m_capacity) { }

			int count() const {
				int w = m_writepos.load(std::memory_order_acquire);
				int p = m_playpos.load(std::memory_order_acquire);

				if (w >= p)
					return w - p;
				else
					return m_capacity + w - p;
			}

			void increment_writepos(int n) {
				int w = m_writepos.load(std::memory_order_relaxed);
				w = (w + n) % m_capacity;
				m_writepos.store(w, std::memory_order_release);
			}

			void increment_playpos(int n) {
				int p = m_playpos.load(std::memory_order_relaxed);
				p = (p + n) % m_capacity;
				m_playpos.store(p, std::memory_order_release);
			}

			int write(const T* src, int n) {
				int available = m_capacity - m_reserve - count();
				n = std::min(n, available);

				if (n == 0) return 0;

				int w = m_writepos.load(std::memory_order_relaxed);

				if (w + n > m_capacity) {
					int first = m_capacity - w;
					std::copy(src, src + first, m_buf.begin() + w);
					std::copy(src + first, src + n, m_buf.begin());
				} else {
					std::copy(src, src + n, m_buf.begin() + w);
				}

				increment_writepos(n);
				return n;
			}

			int peek(T* dst, int n) {
				n = std::min(n, count());

				if (n == 0) return 0;

				int p = m_playpos.load(std::memory_order_relaxed);

				if (p + n > m_capacity) {
					int first = m_capacity - p;
					std::copy(m_buf.begin() + p, m_buf.begin() + m_capacity, dst);
					std::copy(m_buf.begin(), m_buf.begin() + (n - first), dst + first);
				} else {
					std::copy(m_buf.begin() + p, m_buf.begin() + (p + n), dst);
				}

				return n;
			}

			int read(T* dst, int n) {
				n = peek(dst, n);

				increment_playpos(n);

				return n;
			}
		};

		int m_rate;
		uint32_t m_channels;
		int m_buffer_min_ct;
		int m_skip_threshold;
		osd_ticks_t m_osd_ticks;
		osd_ticks_t m_skip_threshold_ticks;
		std::unique_ptr<buffer<int16_t>> m_ab;

		int m_xfade_length;
		std::vector<int16_t> m_xfade_buf;
		int m_xfade_total;
		int m_xfade_remaining;
	};
};

#endif // MAME_OSD_SOUND_SOUND_MODULE_H
