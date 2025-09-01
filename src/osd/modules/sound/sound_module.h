// license:BSD-3-Clause
// copyright-holders:Couriersud
/*
 * sound_module.h
 *
 */
#ifndef MAME_OSD_SOUND_SOUND_MODULE_H
#define MAME_OSD_SOUND_SOUND_MODULE_H

#pragma once

#include <atomic>
#include "emu.h"
#include <osdepend.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#define OSD_SOUND_PROVIDER   "sound"

#define USE_AB   1
#define DEBUG_AB 1

#if USE_AB
template <typename T>
struct audio_buffer {
	T*               buf;
	int              size;
	int              reserve;
	std::atomic<int> playpos, writepos;

	audio_buffer(int size, int reserve) : size(size + reserve), reserve(reserve) {
		playpos = writepos = 0;
		buf = new T[this->size];
	}

	~audio_buffer() { delete[] buf; }

	int count() {
		int diff = writepos - playpos;
		return diff < 0 ? size + diff : diff;
	}

	void increment_writepos(int n) {
		writepos.store((writepos + n) % size);
	}

	int write(const T* src, int n) {
		n = std::min<int>(n, size - reserve - count());

		if (writepos + n > size) {
			std::memcpy(buf + writepos, src, sizeof(T) * (size - writepos));
			std::memcpy(buf, src + (size - writepos), sizeof(T) * (n - (size - writepos)));
		} else {
			std::memcpy(buf + writepos, src, sizeof(T) * n);
		}

		increment_writepos(n);

		return n;
	}

	void increment_playpos(int n) {
		playpos.store((playpos + n) % size);
	}

	int read(T* dst, int n) {
		n = std::min<int>(n, count());

		if (playpos + n > size) {
			std::memcpy(dst, buf + playpos, sizeof(T) * (size - playpos));
			std::memcpy(dst + (size - playpos), buf, sizeof(T) * (n - (size - playpos)));
		} else {
			std::memcpy(dst, buf + playpos, sizeof(T) * n);
		}

		increment_playpos(n);

		return n;
	}

	int clear(int n) {
		n = std::min<int>(n, size - reserve - count());

		if (writepos + n > size) {
			std::memset(buf + writepos, 0, sizeof(T) * (size - writepos));
			std::memset(buf, 0, sizeof(T) * (n - (size - writepos)));
		} else {
			std::memset(buf + writepos, 0, sizeof(T) * n);
		}

		increment_writepos(n);

		return n;
	}
};
#endif

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
	class abuffer {
	public:
		abuffer(uint32_t channels) noexcept;
		abuffer(uint32_t channels, int rate, float audio_latency) noexcept;
		void get(int16_t *data, uint32_t samples) noexcept;
		void push(const int16_t *data, uint32_t samples);
		void clear() noexcept { m_used_buffers = 0; }
		uint32_t channels() const noexcept { return m_channels; }
		uint32_t available() const noexcept;

	private:
		struct buffer {
			uint32_t m_cpos;
			std::vector<int16_t> m_data;

			buffer() noexcept = default;
			buffer(const buffer &) = default;
			buffer(buffer &&) noexcept = default;
			buffer &operator=(const buffer &) = default;
			buffer &operator=(buffer &&) noexcept = default;
		};

#if USE_AB
		u64 callback_ct;
		u64 samples_in;
		u64 samples_out;
		u64 buffer_min_ct;
		u64 skip_threshold = 400;
		bool underflow;
		bool overflow;
		osd_ticks_t skip_threshold_ticks;
		osd_ticks_t m_osd_ticks;
		audio_buffer<int16_t> *ab;
		int allowed_buffer_count;
#endif

		void pop_buffer() noexcept;
		buffer &push_buffer();

		int32_t m_delta, m_delta2;
		uint32_t m_channels;
		uint32_t m_used_buffers;
		std::vector<int16_t> m_last_sample;
		std::vector<buffer> m_buffers;
	};
};

#endif // MAME_OSD_SOUND_SOUND_MODULE_H
