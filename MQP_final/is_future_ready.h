#ifndef ISFUTUREREADY_H
#define ISFUTUREREADY_H

#include <future>

namespace mqp
{
	template<typename T>
	bool is_future_ready(const std::future<T>& f)
	{
		std::future_status status = f.wait_for(std::chrono::seconds(0));

		return status == std::future_status::ready || status == std::future_status::deferred;
	}
}

#endif //ISFUTUREREADY_H
