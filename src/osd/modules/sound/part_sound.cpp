// license:BSD-3-Clause
// copyright-holders:O. Galibert intealls
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

#include <portaudio.h>
#include <mutex>
#include <map>

namespace osd {

namespace {

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

class rtbuf {
public:
	rtbuf(uint32_t channels, int rate, float audio_latency, bool log) noexcept;
	rtbuf(rtbuf&& obj);
	~rtbuf();
	void get(int16_t *data, uint32_t samples) noexcept;
	void push(const int16_t *data, uint32_t samples);

private:
	size_t m_sample_rate;
	uint32_t m_channels;
	u64 m_callback_ct;
	u64 m_samples_in;
	u64 m_samples_out;
	int m_buffer_min_ct;
	int m_skip_threshold;
	bool m_underflow;
	bool m_overflow;
	osd_ticks_t m_skip_threshold_ticks;
	osd_ticks_t m_osd_ticks;
	bool m_log;
	audio_buffer<int16_t> *m_ab;
};

rtbuf::rtbuf(uint32_t channels, int rate, float audio_latency, bool log) noexcept :
	m_sample_rate(rate),
	m_channels(channels),
	m_callback_ct(0),
	m_samples_in(0),
	m_samples_out(0),
	m_buffer_min_ct(0),
	m_skip_threshold(((1.5 + audio_latency * 3.0) / 1000.0) * rate + 0.5f),
	m_underflow(false),
	m_overflow(false),
	m_skip_threshold_ticks(0),
	m_osd_ticks(0),
	m_log(log)
{
	m_ab = new audio_buffer<int16_t>(rate * channels, channels);
}

// move constructor to get it to not break with the stream_info device
rtbuf::rtbuf(rtbuf&& obj) :
	m_sample_rate(obj.m_sample_rate),
	m_channels(obj.m_channels),
	m_callback_ct(obj.m_callback_ct),
	m_samples_in(obj.m_samples_in),
	m_samples_out(obj.m_samples_out),
	m_buffer_min_ct(obj.m_buffer_min_ct),
	m_skip_threshold(obj.m_skip_threshold),
	m_underflow(obj.m_underflow),
	m_overflow(obj.m_overflow),
	m_skip_threshold_ticks(obj.m_skip_threshold_ticks),
	m_osd_ticks(obj.m_osd_ticks),
	m_log(obj.m_log)
{
	m_ab = obj.m_ab;

	obj.m_ab = nullptr;
}

rtbuf::~rtbuf()
{
	if (m_ab != nullptr)
		std::destroy_at(m_ab);
}

void rtbuf::get(int16_t *data, uint32_t samples) noexcept
{
	int buf_ct = m_ab->count() / m_channels;

	if (buf_ct >= samples)
	{
		m_ab->read(data, samples * m_channels);

		// keep track of the minimum buffer count, skip samples adaptively to respect the audio_latency setting
		buf_ct -= samples;

		if (buf_ct < m_buffer_min_ct)
			m_buffer_min_ct = buf_ct;

		// if we are below the threshold, reset the counter
		if (buf_ct < m_skip_threshold)
			m_skip_threshold_ticks = m_osd_ticks;

		// if we have been above the set threshold for ~1 second, skip forward
		if (m_osd_ticks - m_skip_threshold_ticks > osd_ticks_per_second())
		{
			int adjust = m_buffer_min_ct - m_skip_threshold;

			// if adjustment is less than two milliseconds, don't bother
			if (adjust > m_sample_rate / 500) {
				printf("buf_ct: %d\nbuffer_min_ct: %d\nskip_threshold: %d\n", buf_ct, m_buffer_min_ct, m_skip_threshold);
				m_ab->increment_playpos(adjust * m_channels);
				m_overflow = true;
			}

			m_skip_threshold_ticks = m_osd_ticks;
			m_buffer_min_ct = 1e8;
		}
	}
	else
	{
		m_ab->read(data, buf_ct * m_channels);
		std::memset(data + (buf_ct * m_channels), 0, (samples - buf_ct) * m_channels);

		// if update_audio_stream has been called, note the underflow
		if (m_osd_ticks)
			m_underflow = true;

		m_skip_threshold_ticks = m_osd_ticks;
	}

	m_samples_out += samples;
	m_callback_ct = samples;
}

void rtbuf::push(const int16_t *data, uint32_t samples)
{
	m_samples_in += samples;

	int stat = m_overflow ? 1 : m_underflow ? -1 : 0;
	int ct = m_ab->count();

	if (m_overflow)
	{
		osd_printf_verbose("overflow\n");
		m_overflow = false;
	}

	if (m_underflow)
	{
		osd_printf_verbose("underflow\n");
		// add some silence to prevent immediate underflows
		m_ab->clear(m_skip_threshold * m_channels / 2);
		m_underflow = false;
	}
	osd_ticks_t diff = m_osd_ticks;

	// for determining buffer overflows, take the sample here instead of in the callback
	m_osd_ticks = osd_ticks();

	diff = m_osd_ticks - diff;

	if (m_log)
		osd_printf_verbose("123456.123456, %lu, %lu, %lu, %ld, %f\n",
		                   m_samples_out,
		                   m_samples_in,
		                   ct,
		                   stat,
		                   (double) diff / osd_ticks_per_second());

	m_ab->write(data, samples * m_channels);
}

class sound_part : public osd_module, public sound_module
{
public:
	sound_part() : osd_module(OSD_SOUND_PROVIDER, "part")
	{
	}
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

private:
	struct stream_info {
		sound_part *m_manager;
		PaStream *m_stream;
		uint32_t m_channels;
		uint32_t m_id;
		uint32_t m_devid;
		rtbuf m_buffer;

		stream_info(sound_part *manager, uint32_t channels, int rate, float latency, bool log, uint32_t id, uint32_t devid) :
			m_manager(manager), m_stream(nullptr), m_channels(channels), m_id(id), m_devid(devid), m_buffer(channels, rate, latency, log)
		{ }
	};

	osd::audio_info m_info;

	// Used when the structure changes but we're certain the stream callbacks won't be hit.
	// In our case, that means closure callback changing the streams structure.
	std::mutex m_gen_mutex;

	std::map<uint32_t, stream_info> m_streams;

	uint32_t m_stream_id;
	float m_audio_latency;
	float m_pa_latency;
	int m_sample_rate;
	bool m_log;

	emusync* m_emusync;

	int stream_callback(stream_info *stream, const void *input, void *output, unsigned long frameCount, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags);
	static int s_stream_callback(const void *input, void *output, unsigned long frameCount, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData);

	void stream_finished_callback(stream_info *stream);
	static void s_stream_finished_callback(void *userData);
};

int sound_part::init(osd_interface &osd, osd_options const &options)
{
	// Portaudio does not seem to have any information w.r.t
	// channel positioning, so we'll use the sdl conventions.

	enum { FL, FR, FC, LFE, BL, BR, BC, SL, SR, AUX };
	static const char *const posname[10] = { "FL", "FR", "FC", "LFE", "BL", "BR", "BC", "SL", "SR", "AUX" };

	static const osd::channel_position pos3d[10] = {
		osd::channel_position::FL(),
		osd::channel_position::FR(),
		osd::channel_position::FC(),
		osd::channel_position::LFE(),
		osd::channel_position::RL(),
		osd::channel_position::RR(),
		osd::channel_position::RC(),
		osd::channel_position(-0.2,  0.0,  0.0),
		osd::channel_position( 0.2,  0.0,  0.0),
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
		{ FL, FR, AUX, AUX, AUX, AUX, AUX, AUX, AUX }, // pa over alsa emulation of pipewire or pulse is a mess...
	};

	PaError err = Pa_Initialize();
	if(err) {
		osd_printf_error("PortAudio error: %s\n", Pa_GetErrorText(err));
		return 1;
	}

	m_audio_latency = options.audio_latency();
	m_pa_latency = options.part_latency();
	m_sample_rate = options.sample_rate();
	m_log = options.part_log();

	m_info.m_generation = 1;
	m_info.m_nodes.resize(Pa_GetDeviceCount());
	osd_printf_verbose("PortAudio: Found %d devices:\n", Pa_GetDeviceCount());

	auto dc = [](PaDeviceIndex dev) -> int { return dev == paNoDevice ? 0 : dev+1; };
	m_info.m_default_sink = dc(Pa_GetDefaultOutputDevice());
	m_info.m_default_source = 0;

	std::unordered_map<std::string, int> namecount;

	for(PaDeviceIndex dev = 0; dev != Pa_GetDeviceCount(); dev++) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(dev);
		const PaHostApiInfo *ai = Pa_GetHostApiInfo(di->hostApi);

		auto &node = m_info.m_nodes[dev];
		node.m_id = dev + 1;
		node.m_rate.m_default_rate = node.m_rate.m_min_rate = node.m_rate.m_max_rate = m_sample_rate;
		node.m_sinks = di->maxOutputChannels < 2 ? 0 : 2;
		node.m_sources = 0;

		// remove enters from possibly buggy device string
		node.m_name = util::string_format("%s: %s", ai->name, di->name);
		node.m_name.erase(std::remove_if(node.m_name.begin(), node.m_name.end(), [](char c) {
			return c == '\r' || c == '\n';
		}), node.m_name.end());

		// append number to identical names
		namecount[node.m_name]++;
		if(namecount[node.m_name] > 1)
			node.m_name += " (" + std::to_string(namecount[node.m_name]) + ")";
		node.m_display_name = node.m_name;

		osd_printf_verbose("PortAudio: #%d: %s%s(%d outputs)\n",
				node.m_id,
				node.m_name,
				(node.m_id == m_info.m_default_sink || node.m_id == m_info.m_default_source) ? " (default) " : " ",
				node.m_sinks);

		int channels = std::max(node.m_sinks, node.m_sources);
		int index = std::min(channels, 9) - 1;
		for(uint32_t port = 0; port != channels; port++) {
			uint32_t pos = positions[index][std::min(8U, port)];
			node.m_port_names.push_back(posname[pos]);
			node.m_port_positions.push_back(pos3d[pos]);
		}
	}

	m_stream_id = 1;

	m_emusync = &downcast<osd_common_t &>(osd).machine().sync();

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
	if(node < 1 || node > m_info.m_nodes.size())
		return 0;

	osd_printf_verbose("PART: Opening sink on %s\n", m_info.m_nodes[node-1].m_display_name);

	uint32_t id = m_stream_id ++;
	auto si = m_streams.emplace(id, stream_info(this, m_info.m_nodes[node-1].m_sinks, m_sample_rate, m_audio_latency, m_log, id, node)).first;

	PaStreamParameters op;
	op.device = node - 1;
	op.channelCount = m_info.m_nodes[node-1].m_sinks;
	op.sampleFormat = paInt16;
	op.suggestedLatency = (m_pa_latency > 0.0f) ? m_pa_latency : Pa_GetDeviceInfo(node - 1)->defaultLowOutputLatency;
	op.hostApiSpecificStreamInfo = nullptr;

	PaError err = Pa_OpenStream(&si->second.m_stream, nullptr, &op, rate, paFramesPerBufferUnspecified, 0, s_stream_callback, &si->second);
	if(!err)
		err = Pa_SetStreamFinishedCallback(si->second.m_stream, s_stream_finished_callback);
	if(!err)
		err = Pa_StartStream(si->second.m_stream);
	if(err) {
		osd_printf_error("PortAudio error: %s: %s\n", m_info.m_nodes[node-1].m_display_name, Pa_GetErrorText(err));
		lock.unlock();
		stream_close(id);
		return 0;
	}
	return id;
}

uint32_t sound_part::stream_source_open(uint32_t node, std::string name, uint32_t rate)
{
	return 0;
}

void sound_part::stream_close(uint32_t id)
{
	std::unique_lock<std::mutex> lock(m_gen_mutex);
	auto si = m_streams.find(id);
	if(si == m_streams.end())
		return;
	if(auto *s = si->second.m_stream; s) {
		lock.unlock();
		Pa_CloseStream(s);
	}
	else
		m_streams.erase(si);
}

void sound_part::stream_sink_update(uint32_t id, const int16_t *buffer, int samples_this_frame)
{
	auto si = m_streams.find(id);
	if(si == m_streams.end())
		return;
	si->second.m_buffer.push(buffer, samples_this_frame);
}

void sound_part::stream_source_update(uint32_t id, int16_t *buffer, int samples_this_frame)
{
	return;
}

int sound_part::stream_callback(stream_info *stream, const void *input, void *output, unsigned long frameCount, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags)
{
	if(output) {
		m_emusync->register_sink_samples(stream->m_id, frameCount);
		stream->m_buffer.get((int16_t *)output, frameCount);
	}
	return 0;
}

int sound_part::s_stream_callback(const void *input, void *output, unsigned long frameCount, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData)
{
	stream_info *si = (stream_info *)userData;
	return si->m_manager->stream_callback(si, input, output, frameCount, timeInfo, statusFlags);
}

void sound_part::stream_finished_callback(stream_info *stream)
{
	std::unique_lock<std::mutex> lock(m_gen_mutex);
	auto si = m_streams.find(stream->m_id);
	if(si == m_streams.end())
		return;
	m_streams.erase(si);
}

void sound_part::s_stream_finished_callback(void *userData)
{
	stream_info *si = (stream_info *)userData;
	return si->m_manager->stream_finished_callback(si);
}


} // anonymous namespace

} // namespace osd

#else

namespace osd { namespace { MODULE_NOT_SUPPORTED(sound_part, OSD_SOUND_PROVIDER, "part") } }

#endif

MODULE_DEFINITION(SOUND_PART, osd::sound_part)
