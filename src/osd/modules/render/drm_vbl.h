// license:BSD-3-Clause
// copyright-holders: Antonio Giner, intealls
/***************************************************************************

    drm_vbl.h

    DRM vblank handling

***************************************************************************/

#ifdef SDLMAME_X11
#ifndef SRC_OSD_MODULES_RENDER_DRM_VBL_H_
#define SRC_OSD_MODULES_RENDER_DRM_VBL_H_

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <queue>

#include "modules/lib/osdobj_common.h"

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <unistd.h>

using namespace osd;

template<typename T>
class blocking_queue {
public:
	blocking_queue(size_t max_size) : max_size_(max_size), count_(0) {}

	void push(const T& item) {
		std::unique_lock<std::mutex> lock(mutex_);

		// block until there's space in the queue
		cond_full_.wait(lock, [this](){ return count_ < max_size_; });

		queue_.push(item);
		++count_;

		cond_empty_.notify_one();
	}

	T pop() {
		std::unique_lock<std::mutex> lock(mutex_);

		// block until the queue is not empty
		cond_empty_.wait(lock, [this](){ return count_ > 0; });

		T item = queue_.front();
		queue_.pop();
		--count_;

		cond_full_.notify_one();

		return item;
	}

	size_t count() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return count_;
	}

	bool full() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return count_ == max_size_;
	}
private:
	std::queue<T> queue_;
	size_t max_size_;
	size_t count_;
	mutable std::mutex mutex_;
	std::condition_variable cond_empty_;
	std::condition_variable cond_full_;
};

class drm_vblank_handler {
public:
	drm_vblank_handler(const char* dri_device, bool use_thread);
	~drm_vblank_handler();
	bool is_open();
	void drm_waitvblank(int crtc, int single_force_crtc);
private:
	int dri_fd;
	const char* dri_device;
	bool use_vbl_thread;
	std::atomic<bool> init_vbl_thread;
	std::atomic<bool> kill_vbl_thread;
	std::thread* m_vblthread;
	blocking_queue<u64>* vbl_queue;

	int drm_open(const char *dri_device);
	void set_up_drmvbl_request(drmVBlank* vbl, int crtc, int single_force_crtc);
	void vbl_thread_func(const int crtc, const int single_force_crtc);
};

#endif /* SRC_OSD_MODULES_RENDER_DRM_VBL_H_ */
#endif
