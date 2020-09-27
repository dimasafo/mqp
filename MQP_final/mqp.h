#ifndef MQP_H
#define MQP_H

#include <list>
#include <future>
#include <set>
#include <unordered_map>

#include  "imqp.h"
#include "async_task_loop.h"

namespace mqp
{
	template<typename Key, typename Value, typename Consumer, typename Queue = std::list<Value>, size_t MAX_Q_CAPACITY = 1000>
	class MQP : public IMQP<Key, Value, Consumer>
	{
		struct Data;

	public:
		MQP(size_t consumersThreadNumber = 0)
		{
			if (consumersThreadNumber == 0)
				threadNumber_ = std::thread::hardware_concurrency();
			else
				threadNumber_ = consumersThreadNumber;

			Run();
		}

		virtual ~MQP()
		{
			try
			{
				Stop();
			}
			catch (...)
			{
				//nothing
			}
		}

		virtual bool Subscribe(const Key& id, Consumer* consumer)
		{
			if (!consumer)
			{
				Unsubscribe(id);
				return true;
			}

			Data* data = nullptr;
			{
				std::unique_lock<std::mutex> lockMap(keyToDataChanges_);

				dummyDataPtr.data->key = id;
				auto it = keyToDataMap_.find(dummyDataPtr);

				if (it == keyToDataMap_.end())
				{
					std::unique_lock<std::mutex> lockData(anyDataChanges_);

					dataPool_.emplace_back(id);
					data = &(dataPool_.back());
					DataPtr dataPtr{ data };
					keyToDataMap_.insert(dataPtr);

					return addConsumer_unsafe(data, consumer);
				}

				data = (*it).data;
			}

			std::unique_lock<std::mutex> lockData(anyDataChanges_);
			return addConsumer_unsafe(data, consumer);
		}

		virtual void Unsubscribe(const Key& id)
		{
			Data* data = nullptr;
			{
				std::unique_lock<std::mutex> lockMap(keyToDataChanges_);

				dummyDataPtr.data->key = id;
				auto it = keyToDataMap_.find(dummyDataPtr);

				if (it == keyToDataMap_.end())
					return;

				data = (*it).data;
			}

			if (data)
			{
				std::unique_lock<std::mutex> lockData(anyDataChanges_);

				data->consumer = nullptr;
			}
		}

		virtual bool Enqueue(const Key& id, const Value& value)
		{
			Data* data = nullptr;

			{
				std::unique_lock<std::mutex> lockMap(keyToDataChanges_);

				dummyDataPtr.data->key = id;
				auto it = keyToDataMap_.find(dummyDataPtr);

				if (it == keyToDataMap_.end())
				{
					std::unique_lock<std::mutex> lockData(anyDataChanges_);

					dataPool_.emplace_back(id);
					data = &(dataPool_.back());
					DataPtr dataPtr{ data };
					keyToDataMap_.insert(dataPtr);

					return enqueueData_unsafe(data, value);
				}

				data = (*it).data;
			}

			ExitScopeNotifier<std::condition_variable> notifier(push_notifier_);

			std::unique_lock<std::mutex> lockData(anyDataChanges_);
			return enqueueData_unsafe(data, value);
		}

		virtual Value Dequeue(const Key& id)
		{
			Data* data = nullptr;
			{
				std::unique_lock<std::mutex> lockMap(keyToDataChanges_);

				dummyDataPtr.data->key = id;
				auto it = keyToDataMap_.find(dummyDataPtr);

				if (it == keyToDataMap_.end())
					return defaultValue_;

				data = (*it).data;
			}

			std::unique_lock<std::mutex> lockData(anyDataChanges_);
			auto& queue = data->queue;

			return dequeue_unsafe(queue);
		}

		virtual void Run()
		{
			myAsyncTask_.start([this]() { Process(); });
		}

		virtual void Stop()
		{
			push_notifier_.notify_one();

			myAsyncTask_.stop(true);
		}

		virtual void WaitConsumeAll()
		{
			myAsyncTask_.start([this]() { Process(); });

			while (!myAsyncTask_.is_done())
			{
				push_notifier_.notify_one();

				std::unique_lock<std::mutex> lockMap(keyToDataChanges_);
				std::unique_lock<std::mutex> lockData(anyDataChanges_);

				if (canProcess_unsafe())
					continue;

				return;
			}
		}

	protected:
		virtual bool addConsumer_unsafe(Data* data, Consumer* consumer)
		{
			if (data->consumer)
			{
				return false;
			}

			data->consumer = consumer;

			return true;
		}

		virtual bool enqueueData_unsafe(Data* data, const Value& value)
		{
			if (data->queue.size() < MAX_Q_CAPACITY)
			{
				data->queue.push_back(value);

				return true;
			}

			return false;
		}

		virtual Value dequeue_unsafe(Queue& queue)
		{
			if (queue.size() > 0)
			{
				//здесь мы лишний раз храним value. можно ли это как-то обойти?
				auto front = queue.front();
				queue.pop_front();
				return front;
			}

			return defaultValue_;
		}

		virtual bool canProcess_unsafeMap()
		{
			if (myAsyncTask_.is_stopping())
				return true;

			if (!anyDataChanges_.try_lock())
			{
				return false;
			}

			std::lock_guard<std::mutex> lockData(anyDataChanges_, std::adopt_lock);

			return canProcess_unsafe();
		}

		virtual bool canProcess_unsafe()
		{
			for (const auto& val : keyToDataMap_)
			{
				if (val.data->consumer && !(val.data->queue).empty())
					return true;
			}

			return false;
		}

		virtual void Process()
		{
			std::vector<Data*>  todoExec_;

			Process_prepareExecList(todoExec_);

			Process_runExecList(todoExec_);

			if (myAsyncTask_.is_stopping())
			{
				checkAllConsumersTasks();
			}
		}

		virtual bool Process_prepareExecList(std::vector<Data*>& todoExec_)
		{
			if (myAsyncTask_.is_stopping())
				return false;

			std::unique_lock<std::mutex> lockMap(keyToDataChanges_);
			push_notifier_.wait(lockMap, [this]() { return canProcess_unsafeMap(); });

			if (myAsyncTask_.is_stopping())
				return false;


			todoExec_.clear();
			todoExec_.reserve(keyToDataMap_.size());

			for (const auto& val : keyToDataMap_)
			{
				todoExec_.emplace_back(val.data);
			}


			if (todoExec_.empty())
				return false;

			return true;
		}

		virtual void Process_runExecList(std::vector<Data*>& todoExec_)
		{
			while (!todoExec_.empty())
			{
				if (myAsyncTask_.is_stopping())
					return;

				auto data = todoExec_.back();
				{
					std::unique_lock<std::mutex> lockData(anyDataChanges_);
					if (!data->consumer)
					{
						todoExec_.pop_back();
						continue;
					}

					if (data->queue.empty())
					{
						todoExec_.pop_back();
						continue;
					}
				}

				while (!addConsumerTask(data))
				{
					checkConsumersTasks();
				}

				todoExec_.pop_back();
			}
		}

		virtual void checkAllConsumersTasks()
		{
			std::unique_lock<std::mutex> ctLock(consumerTasksChanges_);

			for (auto it = consumerTasks_.begin(); it != consumerTasks_.end(); ++it)
			{
				(it->second).future.get();
			}

			consumerTasks_.clear();
		}

		virtual void checkConsumersTasks()
		{
			std::unique_lock<std::mutex> ctLock(consumerTasksChanges_);

			for (auto it = consumerTasks_.begin(); it != consumerTasks_.end(); ++it)
			{
				if (is_future_ready((it->second).future))
				{
					(it->second).future.get();

					consumerTasks_.erase(it);
					return;
				}
			}
		}

		virtual bool addConsumerTask(Data* data)
		{
			std::unique_lock<std::mutex> ctLock(consumerTasksChanges_);

			if (consumerTasks_.size() >= threadNumber_)
				return false;

			auto& task = consumerTasks_[data];

			task.future = std::async(std::launch::async, &MQP::consumerTask, this, data);
			return true;
		}

		virtual void consumerTask(Data* data)
		{
			std::unique_lock<std::mutex> lockData(anyDataChanges_);

			if (!data->consumer)
			{
				return;
			}

			if (data->queue.empty())
			{
				return;
			}

			Value value = dequeue_unsafe(data->queue);
			Key key = data->key;
			Consumer* consumer = data->consumer;

			lockData.unlock();

			consumer->Consume(key, value);
		}

		struct Data
		{
			Data(const Key& id)
				:key(id)
			{
			}

			Consumer* consumer = nullptr;
			Queue queue;
			Key key;
		};

		struct DataPtr
		{
			Data* data = nullptr;

			bool operator<(const DataPtr& rhs) const
			{
				return (data->key) < ((rhs.data)->key);
			}
		};

		struct ConsumerTask
		{
			std::future<void> future;
			Data* data = nullptr;
		};
		std::unordered_map<Data*, ConsumerTask> consumerTasks_;

		std::list<Data> dataPool_;

		Value defaultValue_;


		std::set<DataPtr> keyToDataMap_;
		std::mutex keyToDataChanges_;
		Data dummyData{ Key{} };
		DataPtr dummyDataPtr{ &dummyData };

		std::mutex anyDataChanges_;
		std::mutex consumerTasksChanges_;

		std::condition_variable push_notifier_;

		std::atomic<bool> stopping = false;

		AsyncTaskLoop myAsyncTask_;

		size_t threadNumber_;

		template<typename COND>
		struct ExitScopeNotifier
		{
			ExitScopeNotifier(COND& cond)
				: cond_(cond) {}

			~ExitScopeNotifier()
			{
				cond_.notify_one();
			}

			COND& cond_;
		};

	private:
		MQP(const MQP& src) = delete;
		MQP& operator=(const MQP& src) = delete;
	};
}

#endif //MQP_H