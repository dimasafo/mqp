#ifndef IMQP_H
#define IMQP_H

namespace mqp
{
	template<typename Key, typename Value, typename Consumer>
	struct IMQP
	{
		virtual ~IMQP() {}

		virtual bool Subscribe(const Key& id, Consumer* consumer) = 0;
		virtual void Unsubscribe(const Key& id) = 0;
		virtual bool Enqueue(const Key& id, const Value& value) = 0;
		virtual Value Dequeue(const Key& id) = 0;
		virtual void Run() = 0;
		virtual void Stop() = 0;
		virtual void WaitConsumeAll() = 0;
	};
}

#endif //IMQP_H
