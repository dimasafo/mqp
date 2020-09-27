//#include <string>

#include "mqp.h" 

template<typename Key, typename Value>
struct IConsumer
{
	IConsumer(int delay)
		: delay_(delay)
	{
	}

	virtual void Consume(Key id, const Value& value)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(delay_)); //imitating work
	}

	int delay_;
};

int main()
{
	//mqp::MQP<int, std::string, IConsumer<int, std::string>> mqp;

	//IConsumer<int, std::string> c(100);

	//mqp.Enqueue(123, "456");
	//mqp.Subscribe(123, &c);
	//mqp.WaitConsumeAll();

    return 0;
}