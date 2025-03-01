#include "fixedThreadPool.h"
#include <iostream>

namespace ThreadPools {

FixedThreadPool::FixedThreadPool(size_t numThreads) {
	numThreads = std::max(numThreads, static_cast<size_t>(1));
	for (size_t i = 0; i < numThreads; ++i) {
		workers_.push(std::thread(&FixedThreadPool::worker_loop_, this));
	}
}

FixedThreadPool::~FixedThreadPool() {
	stop();
}

void FixedThreadPool::stop(bool wait) {
	if (isTerminated_.load())
		return;
	ifWait_.store(wait);
	isTerminated_.store(true);
	task_cond_.notify_all();
	std::thread t;
	while (workers_.try_pop(t)) {
		if(t.joinable())
			t.join();
	}
}

void FixedThreadPool::worker_loop_() {
	Task task;
	while (true) {
		{
			std::unique_lock<std::mutex> lock(taskMutex_);
			task_cond_.wait(lock, [this] {return !tasks_.empty() || isTerminated_.load(); });
			if (isTerminated_.load()) {
				// ����������Ϊ�գ����˳��߳�
				// �������Ϊ�գ����Ҳ���ȴ������˳��߳�
				if (tasks_.empty() || !ifWait_.load()) {
					return;
				}
			}
			task = std::move(tasks_.back());
			tasks_.pop_back();
		}
		task();
	}
}

} // namespace ThreadPools