#ifndef ASUNC_TASK_LOOP
#define ASUNC_TASK_LOOP

#include <future>
#include <thread>
#include <memory>

#include "is_future_ready.h"

namespace mqp
{
	class AsyncTaskLoop
	{
	public:
		AsyncTaskLoop() = default;
		~AsyncTaskLoop()
		{
			stop(false);
		}

		void stop(bool rethrow = true)
		{
			if (is_done())
				return;

			if (!thread2_)
				return;

			stopping = true;

			std::thread* thread = thread2_.release();

			try
			{
				future_.get();
			}
			catch (...)
			{
				thread->join();

				if (rethrow)
				{
					throw std::current_exception();
				}
				return;
			}

			thread->join();
		}

		template<typename Func>
		void thFunc(std::promise<bool>&& first_promise, Func func)
		{
			std::promise<bool> curentPromise(std::move(first_promise));

			while (!is_done())
			{
				try
				{
					func();
				}
				catch (...)
				{
					try {
						curentPromise.set_exception(std::current_exception());
					}
					catch (...) {} // set_exception() may throw too

					return;
				}
			}

			curentPromise.set_value(true);
		}

		template<typename Func>
		void start(Func func)
		{
			if (!is_done())
				return;

			stopping = false;

			std::promise<bool> currentPromise;
			future_ = currentPromise.get_future();

			thread2_ = std::make_unique<std::thread>(std::thread(&AsyncTaskLoop::thFunc<Func>, this, std::move(currentPromise), func));
		}

		bool is_done()
		{
			return !future_.valid() || is_future_ready(future_);
		}

		bool is_stopping()
		{
			return stopping;
		}

	protected:
		std::unique_ptr<std::thread> thread2_;

		std::future<bool> future_;
		std::atomic<bool> stopping = false;

	private:
		AsyncTaskLoop(const AsyncTaskLoop& src) = delete;
		AsyncTaskLoop& operator=(const AsyncTaskLoop& src) = delete;
	};
}

#endif //ASUNC_TASK_LOOP