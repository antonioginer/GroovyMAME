struct raster_status
{
	uint64_t count;
	double scan;
};

class raster_sync
{
public:

	raster_sync();
	~raster_sync() {};

	enum event_tag
	{
		BEFORE_DRAW,
		AFTER_DRAW,
		BEFORE_PRESENT,
		AFTER_PRESENT,
		TIMESTAMP_ITEMS
	};

	void reset();
	uint64_t get_tag(enum raster_sync::event_tag tag);
	void register_tag(enum raster_sync::event_tag timestamp_event);
	bool register_vblank_in_ticks(uint64_t sync_count, uint64_t timestamp);
	bool register_vblank_in_ns(uint64_t sync_count, uint64_t timestamp);
	void register_emutime(uint64_t emutime);
	uint64_t wait_raster(uint64_t count, double scan);
	void get_raster(raster_status *status);
	uint64_t period() { return m_mean > 0? (uint64_t)m_mean : 1e9 / 60; };
	double period_in_ms() { return get_ms(period()); };
	double frame_time_in_ms() { return get_ms(m_frame_time); };
	double auto_framedelay();

private:

	inline double get_ms(int64_t time) { return (double)time / 1e6; };
	inline uint64_t time_in_ns() { return osd_ticks() * ticks_to_ns; };

	bool m_initialized = false;
	bool m_sleep_allowed = false;

	// All timestamps in nanoseconds
	uint64_t m_timestamp[static_cast<int>(TIMESTAMP_ITEMS)] {};
	uint64_t m_first_sync_count = 0;
	uint64_t m_first_timestamp = 0;
	uint64_t m_last_sync_count = 0;
	uint64_t m_last_timestamp = 0;
	uint64_t m_last_count = 0;

	uint64_t m_current_emulation_time;
	uint64_t m_emulation_time[16] = {};
	uint64_t m_emulation_time_avg = 0;
	uint64_t m_emulation_time_dm = 0;
	uint64_t m_frame_time = 0;

	int64_t m_vblank_count = 0;
	int64_t m_current_period = 0;
	int64_t m_mean = 0;
	uint64_t m_fd_margin = 1e6; // 1 ms

	int ticks_to_ns = 0;
	uint64_t sleep_time = 1e6; // 1 ms
};
