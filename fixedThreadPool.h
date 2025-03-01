#pragma once

#include "threadsafe_q.h"

#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <future>
#include <exception>

namespace ThreadPools {

class FixedThreadPool {
public:
	using Task = std::function<void()>;
	FixedThreadPool(size_t numThreads = std::thread::hardware_concurrency());
	virtual ~FixedThreadPool();

	template<typename F, typename... Args>
	auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;
	void stop(bool wait = true);
private:
	details::threadsafe_queue<std::thread> workers_;
	std::vector<Task> tasks_;
	std::mutex taskMutex_;
	std::condition_variable task_cond_;
	std::atomic<bool> isTerminated_{ false };
	std::atomic<bool> ifWait_{ true };

	void worker_loop_();

};

template<typename F, typename... Args>
auto FixedThreadPool::submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {

	if (isTerminated_) {
		throw std::runtime_error("ThreadPool is terminated");
	}

	using return_type = std::invoke_result_t<F, Args...>;
	auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
	std::future<return_type> res = task->get_future();
	{
		std::unique_lock<std::mutex> lock(taskMutex_);
		tasks_.emplace_back([task](){ (*task)(); });
	}
	task_cond_.notify_one();
	return res;
}

} // namespace ThreadPools