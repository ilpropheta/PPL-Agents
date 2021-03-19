#pragma once
#include "Agent.h"
#include "AgentComposer.h"

namespace Agents
{
	// An agent implementing the classical pattern:
	// "consume all messages from an async source by performing an action on them all individually".
	// It supports also a policy for consuming latest messages (those staying in the queue after stopping the agent).
	// The consumer can be stopped by throwing an exception. In this case, its buffer will be linked to a "null buffer".
	// 
	// Basically, you define a "Consumer" class like:
	//
	//
	// class MyConsumer
	// {
	// public:
	//    void Consume(int value)
	//    {
	//       std::cout << "received: " << value << "\n";
	//    }
	// protected:
	//    Concurrency::unbounded_buffer<int> m_buffer; // this name is mandatory
	// };
	//
	// AsyncConsumerAgent<MyConsumer> agent;
	// agent.Start();
	// ...
	// agent.StopAndWait();
	//
	// This one will ignore values received after being stopped:
	// AsyncConsumerAgent<MyConsumer, DropLastValues> agent;
	// 
	template<typename Consumer, typename LastMessagesPolicy = Skills::RetainLastValues>
	class AsyncConsumerAgent : public Consumer, public Agent
	{
	public:
		using Consumer::Consumer;
	protected:
		void Run(CancellationToken& cancellationToken) override
		{
			auto& buffer = this->m_buffer;
			using payloadType = decltype(Utils::detect(buffer));

			try
			{
				payloadType received{};
				while (Receive(buffer, cancellationToken, received))
				{
					this->Consume(received);
				}
				LastMessagesPolicy::Process(buffer, [this](auto val) {
					this->Consume(val);
				});
			}
			catch (const std::exception&)
			{
				static Concurrency::overwrite_buffer<payloadType> NullBuffer;
				// since the consumer has failed and this Agent will be AgentStatus::Completed when returning to the caller,
				// we link its buffer to an overwrite_buffer that will behave like a "null consumer"
				buffer.link_target(&NullBuffer);
			}
		}
	};

	// Use AgentComposer to pass Start and Stop skills to AsyncConsumerAgent
	// Examples:
	// using AutoStartAsyncConsumer = AsyncConsumer<MyConsumer, AutoStart, ManualStop, ManualWait, RetainLastValues>
	template<typename Consumer, template <typename> typename StartPolicy, template <typename> typename StopPolicy, template <typename> typename WaitPolicy, typename LastValuesPolicy>
	using AsyncConsumer = AgentComposer<AsyncConsumerAgent<Consumer, LastValuesPolicy>, StartPolicy, WaitPolicy, StopPolicy>;
}