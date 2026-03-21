// license:BSD-3-Clause
// copyright-holders:intealls, O. Galibert, R. Belmont
/***************************************************************************

    part_sound.cpp

    Real-time PortAudio interface.

***************************************************************************/

#include "sound_module.h"

#include "modules/osdmodule.h"

#ifndef NO_USE_PORTAUDIO

#include "emu.h"
#include "emusync.h"

#include <atomic>

#include "modules/lib/osdobj_common.h"
#include "osdcore.h"

#include <memory>
#include <portaudio.h>
#include <mutex>
#include <map>
#include <utf8proc/utf8proc.h>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include "pa_win_wasapi.h"
#endif

namespace osd {

namespace {

template<typename T>
struct audio_buffer
{
	T *buf;
	int size;
	int reserve;
	std::atomic<int> playpos, writepos;

	audio_buffer(int size, int reserve) : size(size + reserve), reserve(reserve)
	{
		playpos.store(0, std::memory_order_relaxed);
		writepos.store(0, std::memory_order_relaxed);
		buf = new T[this->size];
	}

	~audio_buffer()
	{
		delete[] buf;
	}

	int count()
	{
		int w, p;

		w = writepos.load(std::memory_order_acquire);
		p = playpos.load(std::memory_order_acquire);

		int diff = w - p;
		return diff < 0 ? size + diff : diff;
	}

	void increment_writepos(int n)
	{
		int w = writepos.load(std::memory_order_relaxed);
		w += n;

		if (w >= size)
			w -= size;

		writepos.store(w, std::memory_order_release);
	}

	void increment_playpos(int n)
	{
		int p = playpos.load(std::memory_order_relaxed);
		p += n;

		if (p >= size)
			p -= size;

		playpos.store(p, std::memory_order_release);
	}

	int write(const T *src, int n)
	{
		n = std::min<int>(n, size - reserve - count());

		if (n <= 0)
			return 0;

		int w = writepos.load(std::memory_order_relaxed);

		if (w + n > size) {
			int first = size - w;
			std::memcpy(buf + w, src, sizeof(T) * first);
			std::memcpy(buf, src + first, sizeof(T) * (n - first));
		} else {
			std::memcpy(buf + w, src, sizeof(T) * n);
		}

		increment_writepos(n);

		return n;
	}

	int read(T *dst, int n)
	{
		n = std::min<int>(n, count());

		if (n <= 0)
			return 0;

		int p = playpos.load(std::memory_order_relaxed);

		if (p + n > size) {
			int first = size - p;
			std::memcpy(dst, buf + p, sizeof(T) * first);
			std::memcpy(dst + first, buf, sizeof(T) * (n - first));
		} else {
			std::memcpy(dst, buf + p, sizeof(T) * n);
		}

		increment_playpos(n);

		return n;
	}

	int clear(int n)
	{
		n = std::min<int>(n, size - reserve - count());

		if (n <= 0)
			return 0;

		int w = writepos.load(std::memory_order_relaxed);

		if (w + n > size) {
			int first = size - w;
			std::memset(buf + w, 0, sizeof(T) * first);
			std::memset(buf, 0, sizeof(T) * (n - first));
		} else {
			std::memset(buf + w, 0, sizeof(T) * n);
		}

		increment_writepos(n);

		return n;
	}
};

class rtbuf
{
public:
	rtbuf(uint32_t channels, int rate, float audio_latency) noexcept;
	void get(int16_t *data, uint32_t samples) noexcept;
	void push(const int16_t *data, uint32_t samples);
	size_t available() { return m_ab->count(); };
	int skip_threshold() { return m_skip_threshold; }

private:
	int m_sample_rate;
	uint32_t m_channels;
	int m_buffer_min_ct;
	int m_skip_threshold;
	bool m_underflow;
	osd_ticks_t m_skip_threshold_ticks;
	osd_ticks_t m_osd_ticks;
	std::unique_ptr<audio_buffer<int16_t>> m_ab;
};

rtbuf::rtbuf(uint32_t channels, int rate, float buffering_latency) noexcept :
	m_sample_rate(rate),
	m_channels(channels),
	m_buffer_min_ct(0),
	m_skip_threshold((buffering_latency / 1000.0) * rate + 0.5f),
	m_underflow(false),
	m_skip_threshold_ticks(0),
	m_osd_ticks(0)
{
	m_ab = std::make_unique<audio_buffer<int16_t>>(rate * channels, channels);
}

void rtbuf::get(int16_t *data, uint32_t samples) noexcept
{
	int buf_ct = m_ab->count() / m_channels;

	if (buf_ct >= samples) {
		m_ab->read(data, samples * m_channels);

		// keep track of the minimum buffer count, skip samples adaptively to respect the audio_latency setting
		buf_ct -= samples;

		if (buf_ct < m_buffer_min_ct)
			m_buffer_min_ct = buf_ct;

		// if we are below the threshold, reset the counter
		if (buf_ct < m_skip_threshold)
			m_skip_threshold_ticks = m_osd_ticks;

		// if we have been above the set threshold for ~1 second, skip forward
		if (m_osd_ticks - m_skip_threshold_ticks > osd_ticks_per_second()) {
			int adjust = m_buffer_min_ct - m_skip_threshold;

			// if adjustment is less than two milliseconds, don't bother
			if (adjust > m_sample_rate / 500)
				m_ab->increment_playpos(adjust * m_channels);

			m_skip_threshold_ticks = m_osd_ticks;
			m_buffer_min_ct = 1e8;
		}
	} else {
		m_ab->read(data, buf_ct * m_channels);
		std::memset(data + (buf_ct * m_channels), 0,
				(samples - buf_ct) * m_channels * sizeof(int16_t));

		// if update_audio_stream has been called, note the underflow
		if (m_osd_ticks)
			m_underflow = true;

		m_skip_threshold_ticks = m_osd_ticks;
	}
}

void rtbuf::push(const int16_t *data, uint32_t samples)
{
	if (m_underflow) {
		// add some silence to prevent immediate underflows
		m_ab->clear(m_skip_threshold * m_channels / 2);
		m_underflow = false;
	}

	osd_ticks_t diff = m_osd_ticks;

	// for determining buffer overflows, take the sample here instead of in the callback
	m_osd_ticks = osd_ticks();

	diff = m_osd_ticks - diff;

	m_ab->write(data, samples * m_channels);
}

class sound_part: public osd_module, public sound_module
{
public:
	sound_part() : osd_module(OSD_SOUND_PROVIDER, "part") { }
	virtual ~sound_part() { }

	virtual int init(osd_interface &osd, osd_options const &options) override;
	virtual void exit() override;

	virtual bool external_per_channel_volume() override { return false; }
	virtual bool split_streams_per_source() override { return false; }

	virtual uint32_t get_generation() override;
	virtual osd::audio_info get_information() override;
	virtual uint32_t stream_sink_open(uint32_t node, std::string name, uint32_t rate) override;
	virtual uint32_t stream_source_open(uint32_t node, std::string name, uint32_t rate) override;
	virtual void stream_close(uint32_t id) override;
	virtual void stream_sink_update(uint32_t id, const int16_t *buffer, int samples_this_frame) override;
	virtual void stream_source_update(uint32_t id, int16_t *buffer, int samples_this_frame) override;

	static bool m_print_devices;
private:
	struct stream_info {
		sound_part *m_manager;
		PaStream *m_stream;
		uint32_t m_channels;
		uint32_t m_id;
		uint32_t m_devid;
		rtbuf m_buffer;

		stream_info(sound_part *manager, uint32_t channels, int rate, float latency, uint32_t id, uint32_t devid) :
				m_manager(manager),
				m_stream(nullptr),
				m_channels(channels),
				m_id(id),
				m_devid(devid),
				m_buffer(channels, rate, latency) {
		}
	};

	osd::audio_info m_info;

	// Used when the structure changes but we're certain the stream callbacks won't be hit.
	// In our case, that means closure callback changing the streams structure.
	std::mutex m_gen_mutex;

	std::map<uint32_t, stream_info> m_streams;

	uint32_t m_stream_id;
	const float m_buffering_latency = 2.0; // 2 ms
	float m_pa_latency;
	int m_sample_rate;
	PaDeviceIndex m_pa_idx;

	emusync* m_sync;

	int stream_callback(stream_info *stream, const void *input, void *output,
			unsigned long frameCount, const PaStreamCallbackTimeInfo *timeInfo,
			PaStreamCallbackFlags statusFlags);
	static int s_stream_callback(const void *input, void *output,
			unsigned long frameCount, const PaStreamCallbackTimeInfo *timeInfo,
			PaStreamCallbackFlags statusFlags, void *userData);

	void stream_finished_callback(stream_info *stream);
	static void s_stream_finished_callback(void *userData);

	std::string sanitize_name(const char *name);
	PaDeviceIndex list_get_devidx(const char *api_str, const char *device_str);
};

bool sound_part::m_print_devices = true;

std::string sound_part::sanitize_name(const char *name)
{
	std::string out;

	if (!name || !*name)
		return out;

	utf8proc_uint8_t *utf8proc_result = nullptr;
	const utf8proc_option_t options = static_cast<utf8proc_option_t>(
		UTF8PROC_NULLTERM | UTF8PROC_STABLE | UTF8PROC_DECOMPOSE |
		UTF8PROC_STRIPCC | UTF8PROC_STRIPMARK);

	const utf8proc_ssize_t len = utf8proc_map(
		reinterpret_cast<const utf8proc_uint8_t *>(name),
		0,
		&utf8proc_result,
		options);

	if (utf8proc_result) {
		if (len >= 0) {
			for (utf8proc_ssize_t i = 0; utf8proc_result[i] != 0; ++i) {
				const unsigned char c = utf8proc_result[i];
				if (c >= 0x20 && c <= 0x7E)
					out.push_back(static_cast<char>(c));
			}
		}
		free(utf8proc_result);
	}

	return out;
}

PaDeviceIndex sound_part::list_get_devidx(const char *api_str, const char *device_str)
{
	PaDeviceIndex selected_devidx = -1;
	std::unordered_map<std::string, int> name_counts;

	for (PaHostApiIndex api_idx = 0; api_idx < Pa_GetHostApiCount(); api_idx++) {
		const PaHostApiInfo *api_info = Pa_GetHostApiInfo(api_idx);

		if (m_print_devices)
			osd_printf_info("PART: API %s has %d devices\n",
			                api_info->name, api_info->deviceCount);

		for (int api_devidx = 0; api_devidx < api_info->deviceCount; api_devidx++) {
			PaDeviceIndex devidx = Pa_HostApiDeviceIndexToDeviceIndex(api_idx, api_devidx);
			const PaDeviceInfo *device_info = Pa_GetDeviceInfo(devidx);

			std::string clean_name = sanitize_name(device_info->name);

			// handle duplicates
			int &count = name_counts[clean_name];
			count++;
			if (count > 1)
				clean_name += "_" + std::to_string(count);

			if (!strcmp(api_str, api_info->name) &&
			    !strcmp(device_str, clean_name.c_str())) {
				selected_devidx = devidx;
			}

			// fallback to default device for this API
			if (!strcmp(api_str, api_info->name) &&
			    api_devidx == api_info->deviceCount - 1 &&
			    selected_devidx == -1) {
				selected_devidx = api_info->defaultOutputDevice;
			}

			if (m_print_devices) {
				osd_printf_info("PART: %s: \"%s\"%s\n",
				                api_info->name,
				                clean_name.c_str(),
				                api_info->defaultOutputDevice == devidx
				                    ? " (default)"
				                    : "");
			}
		}
	}

	if (selected_devidx < 0) {
		osd_printf_info("PART: Unable to find specified API or device or none set, reverting to default\n");
		return Pa_GetDefaultOutputDevice();
	}

	m_print_devices = false;
	return selected_devidx;
}

int sound_part::init(osd_interface &osd, osd_options const &options)
{
	// Portaudio does not seem to have any information w.r.t
	// channel positioning, so we'll use the sdl conventions.

	enum { FL, FR, FC, LFE, BL, BR, BC, SL, SR, AUX };

	static const char *const posname[10] = {
		"FL", "FR", "FC", "LFE", "BL", "BR", "BC", "SL", "SR", "AUX"
	};

	static const osd::channel_position pos3d[10] = {
		osd::channel_position::FL(),
		osd::channel_position::FR(),
		osd::channel_position::FC(),
		osd::channel_position::LFE(),
		osd::channel_position::RL(),
		osd::channel_position::RR(),
		osd::channel_position::RC(),
		osd::channel_position(-0.2, 0.0, 0.0),
		osd::channel_position(0.2, 0.0, 0.0),
		osd::channel_position::ONREQ()
	};

	static const uint32_t positions[9][9] = {
		{ FC },
		{ FL, FR },
		{ FL, FR, LFE },
		{ FL, FR, BL, BR },
		{ FL, FR, LFE, BL, BR },
		{ FL, FR, FC, LFE, BL, BR },
		{ FL, FR, FC, LFE, BC, SL, SR },
		{ FL, FR, FC, LFE, BL, BR, SL, SR },
		{ FL, FR, AUX, AUX, AUX, AUX, AUX, AUX, AUX },
	};

	PaError err = Pa_Initialize();
	if (err) {
		osd_printf_error("PART error: %s\n", Pa_GetErrorText(err));
		return 1;
	}

	m_pa_idx = list_get_devidx(options.part_api(), options.part_device());

	m_pa_latency = options.audio_latency();
	m_sample_rate = options.sample_rate();

	m_info.m_generation = 1;
	m_info.m_nodes.resize(1);
	m_info.m_default_sink = 1;
	m_info.m_default_source = 0;

	const PaDeviceInfo *di = Pa_GetDeviceInfo(m_pa_idx);
	const PaHostApiInfo *ai = Pa_GetHostApiInfo(di->hostApi);

	auto &node = m_info.m_nodes[0];
	node.m_id = 1;
	node.m_rate.m_default_rate = node.m_rate.m_min_rate = node.m_rate.m_max_rate = m_sample_rate;
	node.m_sinks = di->maxOutputChannels < 2 ? 0 : 2;
	node.m_sources = 0;

	// remove enters from possibly buggy device string
	node.m_name = util::string_format("%s: %s", ai->name, di->name);
	node.m_name.erase(std::remove_if(node.m_name.begin(), node.m_name.end(), [](char c)
		{ return c == '\r' || c == '\n'; }), node.m_name.end());
	node.m_display_name = node.m_name;

	int channels = std::max(node.m_sinks, node.m_sources);
	int index = std::min(channels, 9) - 1;

	for (uint32_t port = 0; port != channels; port++) {
		uint32_t pos = positions[index][std::min(8U, port)];
		node.m_port_names.push_back(posname[pos]);
		node.m_port_positions.push_back(pos3d[pos]);
	}

	m_stream_id = 1;

	m_sync = &downcast<osd_common_t&>(osd).machine().sync();

	return 0;
}

void sound_part::exit()
{
	Pa_Terminate();
	m_info.m_nodes.clear();
}

uint32_t sound_part::get_generation()
{
	std::unique_lock<std::mutex> lock(m_gen_mutex);
	return m_info.m_generation;
}

osd::audio_info sound_part::get_information()
{
	std::unique_lock<std::mutex> lock(m_gen_mutex);
	return m_info;
}

uint32_t sound_part::stream_sink_open(uint32_t node, std::string name, uint32_t rate)
{
	std::unique_lock<std::mutex> lock(m_gen_mutex);
	if (node < 1 || node > m_info.m_nodes.size())
		return 0;

	uint32_t id = m_stream_id++;
	auto si = m_streams.emplace(id, stream_info(this, m_info.m_nodes[node - 1].m_sinks, m_sample_rate, m_buffering_latency, id, node)).first;

	PaStreamInfo *stream_info;
	PaStreamParameters op;

	unsigned long frames_per_callback = paFramesPerBufferUnspecified;
	op.device = m_pa_idx;
	op.channelCount = m_info.m_nodes[node - 1].m_sinks;
	op.sampleFormat = paInt16;
	op.suggestedLatency = (m_pa_latency > 0.0f) ? (m_pa_latency / 1000.0) : Pa_GetDeviceInfo(m_pa_idx)->defaultLowOutputLatency;
	op.hostApiSpecificStreamInfo = nullptr;

#ifdef _WIN32
	const PaDeviceInfo *device_info = Pa_GetDeviceInfo(m_pa_idx);
	PaWasapiStreamInfo wasapi_stream_info;

	// if requested latency is less than 20 ms, we need to use exclusive mode
	if (Pa_GetHostApiInfo(device_info->hostApi)->type == paWASAPI && op.suggestedLatency < 0.020)
	{
		wasapi_stream_info.size = sizeof(PaWasapiStreamInfo);
		wasapi_stream_info.hostApiType = paWASAPI;
		wasapi_stream_info.flags = paWinWasapiExclusive;
		wasapi_stream_info.version = 1;

		op.hostApiSpecificStreamInfo = &wasapi_stream_info;

		// for latencies lower than ~16 ms, we need to use event mode
		if (op.suggestedLatency < 0.016)
		{
			// only way to control output latency with event mode
			frames_per_callback = op.suggestedLatency * m_sample_rate;

			// needed for event mode to work
			op.suggestedLatency = 0;
		}
	}
#endif

	PaError err = Pa_OpenStream(&si->second.m_stream,
	                            nullptr,
	                            &op,
	                            rate,
	                            frames_per_callback,
	                            0,
	                            s_stream_callback,
	                            &si->second);

	if (err) goto error;

	stream_info = (PaStreamInfo*) Pa_GetStreamInfo(si->second.m_stream);

	osd_printf_verbose("PART: Opening device \"%s\"\n", m_info.m_nodes[node - 1].m_display_name);
	osd_printf_verbose("PART: Sample rate is %0.0f Hz, device output latency is %0.2f ms\n",
		stream_info->sampleRate, stream_info->outputLatency * 1000.0);
	osd_printf_verbose("PART: Allowed additional buffering latency is %0.2f ms/%d frames\n",
		si->second.m_buffer.skip_threshold() / (m_sample_rate / 1000.0), si->second.m_buffer.skip_threshold());

	err = Pa_SetStreamFinishedCallback(si->second.m_stream, s_stream_finished_callback);
	if (err) goto error;

	err = Pa_StartStream(si->second.m_stream);
	if (err) goto error;

	return id;

error:
	osd_printf_error("PART error: %s: %s\n", m_info.m_nodes[node - 1].m_display_name, Pa_GetErrorText(err));
	lock.unlock();
	stream_close(id);
	return 0;
}

uint32_t sound_part::stream_source_open(uint32_t node, std::string name, uint32_t rate)
{
	return 0;
}

void sound_part::stream_close(uint32_t id)
{
	std::unique_lock<std::mutex> lock(m_gen_mutex);
	auto si = m_streams.find(id);
	if (si == m_streams.end())
		return;
	if (auto *s = si->second.m_stream; s) {
		lock.unlock();
		Pa_CloseStream(s);
	} else
		m_streams.erase(si);
}

void sound_part::stream_sink_update(uint32_t id, const int16_t *buffer, int samples_this_frame)
{
	auto si = m_streams.find(id);
	if (si == m_streams.end())
		return;
	size_t count = si->second.m_buffer.available();
	si->second.m_buffer.push(buffer, samples_this_frame);
	m_sync->log("PART buffer count before update", m_sync->NOW, (double) count);
}

void sound_part::stream_source_update(uint32_t id, int16_t *buffer, int samples_this_frame)
{
	return;
}

int sound_part::stream_callback(stream_info *stream, const void *input, void *output, unsigned long frameCount,
		const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags)
{
	if (output) {
		m_sync->register_sink_samples(stream->m_id, frameCount);
		stream->m_buffer.get((int16_t*) output, frameCount);
	}
	return 0;
}

int sound_part::s_stream_callback(const void *input, void *output, unsigned long frameCount,
		const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData)
{
	stream_info *si = (stream_info*) userData;
	return si->m_manager->stream_callback(si, input, output, frameCount, timeInfo, statusFlags);
}

void sound_part::stream_finished_callback(stream_info *stream)
{
	std::unique_lock<std::mutex> lock(m_gen_mutex);
	auto si = m_streams.find(stream->m_id);
	if (si == m_streams.end())
		return;
	m_streams.erase(si);
}

void sound_part::s_stream_finished_callback(void *userData)
{
	stream_info *si = (stream_info*) userData;
	return si->m_manager->stream_finished_callback(si);
}

} // anonymous namespace

} // namespace osd

#else

namespace osd { namespace { MODULE_NOT_SUPPORTED(sound_part, OSD_SOUND_PROVIDER, "part") } }

#endif

MODULE_DEFINITION(SOUND_PART, osd::sound_part)
