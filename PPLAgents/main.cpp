#include <iostream>
#include <string>
#include <thread>
#include "Agent.h"
#include "AsyncConsumer.h"
#include "StrategyBasedAsyncConsumer.h"

using namespace std::chrono_literals;
using namespace Agents::Skills;
using namespace Agents;

struct MyConsumer
{
	MyConsumer(Concurrency::unbounded_buffer<int>& b)
		: m_buffer(b)
	{
		
	}

	void Consume(int i)
	{
		std::cout << "MyConsumer is handling: " << i << "\n";
	}
	
	Concurrency::unbounded_buffer<int>& m_buffer;
};

struct MyAgent : Agent
{
protected:
	void Run(CancellationToken& cancellationToken) override
	{
		while (!cancellationToken.IsCancellationRequested())
		{
			std::cout << "MyAgent is counting..." << m_counter++ << "\n";
			std::this_thread::sleep_for(500ms);
		}
		// just return and the agent underneath will call done() automatically
	}
private:
	int m_counter = 0;
};

int main()
{
	{
		// example of using Agent and AgentComposer
		
		using AutoStartAgent = AgentComposer<MyAgent, AutoStart>;

		AutoStartAgent a;
		std::this_thread::sleep_for(2s);
		a.StopAndWait();
	}
	
	{
		// example of using AsyncConsumer
		
		using AutoAllAsyncConsumer = AsyncConsumer<MyConsumer, AutoStart, AutoStop, AutoWait, RetainLastValues>;
		
		Concurrency::unbounded_buffer<int> b;
		AutoAllAsyncConsumer consumer{ b };

		for(auto i=0; i<10; ++i)
		{
			send(b, i);
		}
	}

	{
		// example of using StrategyBasedAsyncConsumer
		
		auto strategy = std::make_unique<CallableConsumerStrategy<std::string>>([](const std::string& s){
			std::cout << "Getting a message from lambda: " << s << "\n";
		});
		
		Concurrency::unbounded_buffer<std::string> strings;
		StrategyBasedAsyncConsumer<std::string> anotherConsumer{ strings, std::move(strategy) };

		for (auto i=0; i<5; ++i)
		{
			send(strings, std::to_string(i));
		}
	}
}