#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <type_traits>

namespace ThreadPools {
namespace details {

template<typename T>
class threadsafe_queue {
public:
	threadsafe_queue() = default;
	threadsafe_queue(const threadsafe_queue&) = delete;
	threadsafe_queue& operator=(const threadsafe_queue&) = delete;

	template<typename U, std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
	void push(U&& item) {
		std::lock_guard<std::mutex> lock(mtx_);
		q_.push(std::forward<U>(item));
		notEmpty_cond_.notify_one();
	}

	bool try_pop(T& item) {
		std::lock_guard<std::mutex> lock(mtx_);
		if (q_.empty()) {
			return false;
		}
		item = std::move(q_.front());
		q_.pop();
		return true;
	}

	void wait_and_pop(T& item) {
		std::unique_lock<std::mutex> lock(mtx_);
		notEmpty_cond_.wait(lock, [this] { return !q_.empty(); });
		item = std::move(q_.front());
		q_.pop();
	}

	bool empty()const {
		std::lock_guard<std::mutex> lock(mtx_);
		return q_.empty();
	}

	size_t size()const {
		std::lock_guard<std::mutex> lock(mtx_);
		return q_.size();
	}
	
private:
	std::queue<T> q_;
	mutable std::mutex mtx_;
	std::condition_variable notEmpty_cond_;
};


} // namespace details
} // namespace ThreadPools