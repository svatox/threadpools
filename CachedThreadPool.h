#pragma once

#include "threadsafe_q.h"

#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <future>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <exception>

#include "threadsafe_list.h"

namespace ThreadPools {

namespace details {

} // namespace details

class CachedThreadPool {
public:
	using Task = std::function<void()>;
	CachedThreadPool(size_t corePoolSize = std::thread::hardware_concurrency(), size_t maxPoolSize = 1024, std::chrono::milliseconds keepAliveTime = std::chrono::milliseconds(1000));
	virtual ~CachedThreadPool();

	template<typename F, typename... Args>
	auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;
	void stop(bool wait = true);
	size_t core_workers_num()const;
	size_t max_workers_num()const;
	size_t workers_num()const;
	size_t idle_workers_num()const;
	size_t tasks_num()const{std::lock_guard<std::mutex> lock(taskMutex_);return tasks_.size();}
private:
	std::chrono::milliseconds keepAliveTime_; // 线程存活时间
	std::atomic<size_t> coreWorkersNum_; // 核心线程数
	std::atomic<size_t> maxWorkersNum_; // 最大线程数
	std::atomic<size_t> idleWorkersNum_; // 空闲线程数
	mutable std::mutex workersMutex_;
	std::unordered_map<std::thread::id, std::thread> workers_; // 用来存放线程
	
	// 用于清理即将退出的线程
	details::threadsafe_queue<std::thread::id> exitingWorkers_; // 存放要退出的线程id
	std::thread exiting_worker_cleaner_; // 清理空闲线程的线程
	std::mutex cleanerMutex_;
	std::condition_variable cleaner_cond_;


	std::vector<Task> tasks_; // 任务队列
	mutable std::mutex taskMutex_;
	std::condition_variable task_cond_;
	std::atomic<bool> isTerminated_{ false };
	std::atomic<bool> ifWait_{ true };

	void worker_loop_();
	void cleaner_loop_();
	void check_and_add_worker_();
};




template<typename F, typename... Args>
auto CachedThreadPool::submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
	using return_type = std::invoke_result_t<F, Args...>;

	auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
	std::future<return_type> res = task->get_future();

	check_and_add_worker_();

	{
		std::lock_guard<std::mutex> lock(taskMutex_);
		tasks_.emplace_back([task]() { (*task)(); });
	}


	task_cond_.notify_one();

	return res;
}







}