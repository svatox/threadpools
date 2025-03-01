#include "CachedThreadPool.h"
#include <iostream>
#include <cstdio>

namespace ThreadPools {

size_t CachedThreadPool::core_workers_num() const { return coreWorkersNum_.load(); }
size_t CachedThreadPool::max_workers_num() const { return maxWorkersNum_.load(); }
size_t CachedThreadPool::workers_num() const {
	std::lock_guard<std::mutex> lock(workersMutex_);
	return workers_.size();
}
size_t CachedThreadPool::idle_workers_num() const { return idleWorkersNum_.load(); }

CachedThreadPool::CachedThreadPool(size_t corePoolSize, size_t maxPoolSize, std::chrono::milliseconds keepAliveTime) {
	coreWorkersNum_.store(corePoolSize);
	maxWorkersNum_.store(maxPoolSize);
	keepAliveTime_ = keepAliveTime;
	idleWorkersNum_.store(0);

	for (size_t i = 0; i < coreWorkersNum_.load(); ++i) {
		std::thread t(&CachedThreadPool::worker_loop_, this);

		std::lock_guard<std::mutex> lock(workersMutex_);
		workers_[t.get_id()] = std::move(t);
		idleWorkersNum_.fetch_add(1);
	}

	exiting_worker_cleaner_ = std::thread(&CachedThreadPool::cleaner_loop_, this);
}

CachedThreadPool::~CachedThreadPool() {
	stop(true);
}

void CachedThreadPool::stop(bool wait) {
	ifWait_.store(wait);
	isTerminated_.store(true);
	task_cond_.notify_all();

	for (auto& worker : workers_) {
		if (worker.second.joinable()) {
			worker.second.join();
		}
	}
	if(exiting_worker_cleaner_.joinable())
		exiting_worker_cleaner_.join();
}

void CachedThreadPool::worker_loop_() {
	Task task;
	while (true) {
		{
			std::unique_lock<std::mutex> lock(taskMutex_);
			if (false == task_cond_.wait_for(lock, keepAliveTime_, [this]() { return isTerminated_.load() || !tasks_.empty(); })) {
				// 如果超时，先判断线程数量是否小于最小值
				std::lock_guard<std::mutex> lock(workersMutex_);
				if (workers_.size() > coreWorkersNum_.load())
					break;
				else
					continue;
			}
			// 如果线程池结束了
			if (isTerminated_.load()) {
				// 如果任务队列为空，则退出
				// 如果不为空并且不想等待，则退出
				if (tasks_.empty() || !ifWait_.load()) {
					break;
				}
			}
			// 能执行到这里，说明任务队列不为空
			task = std::move(tasks_.back());
			tasks_.pop_back();
		}
		idleWorkersNum_.fetch_sub(1);
		task();
		idleWorkersNum_.fetch_add(1);
	}
	// 线程退出时，将自己加入退出队列，等待cleaner线程处理
	idleWorkersNum_.fetch_sub(1);
	exitingWorkers_.push(std::this_thread::get_id());
	cleaner_cond_.notify_one();
}

void CachedThreadPool::cleaner_loop_() {
	std::thread t;
	std::thread::id id;
	while (true) {
		{
			std::unique_lock<std::mutex> lock(cleanerMutex_);
			cleaner_cond_.wait(lock, [this]() { return isTerminated_.load() || !exitingWorkers_.empty(); });
			if (isTerminated_.load()) {
				return;
			}
		}
		
		// 因为它是线程安全的，所以不需要加锁
		exitingWorkers_.wait_and_pop(id);
		
		{
			std::lock_guard<std::mutex> lock(workersMutex_);
			t = std::move(workers_[id]);
			workers_.erase(id);
		}
		if (t.joinable())
			t.join();
	}
}


void CachedThreadPool::check_and_add_worker_() {
	
	{
		std::lock_guard<std::mutex> lock(workersMutex_);
		if (idleWorkersNum_.load() > 0 || workers_.size() >= maxWorkersNum_.load())
		{
			
			return;
		}
	}

	std::thread t(&CachedThreadPool::worker_loop_, this);
	{
		std::cout << "+++++++++++++  add worker  +++++++++++++++" << std::endl;
		std::lock_guard<std::mutex> lock(workersMutex_);
		workers_[t.get_id()] = std::move(t);
	}

	idleWorkersNum_.fetch_add(1);
}

} // namespace ThreadPools