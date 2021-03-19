#pragma once
#include "AsyncConsumer.h"

namespace Agents
{
	// An AsyncConsumer capable of using a polymorphic strategy

	// Consume behavior abstracted here
	template<typename T>
	class IAsyncConsumerStrategy
	{
	public:
		virtual ~IAsyncConsumerStrategy() = default;
		virtual void Consume(const T&) = 0;
	};

	template<typename T>
	class CallableConsumerStrategy : public IAsyncConsumerStrategy<T>
	{
	public:
		explicit CallableConsumerStrategy(std::function<void(const T&)> action)
			: m_action(action)
		{
			
		}
		
		void Consume(const T& value) override
		{
			m_action(value);
		}
	private:
		std::function<void(const T&)> m_action;
	};

	// This is the consumer that AsyncConsumer will be based on
	template<typename T>
	class ConsumerWithStrategy
	{
	public:
		ConsumerWithStrategy(Concurrency::ISource<T>& src, std::unique_ptr<IAsyncConsumerStrategy<T>> strategy)
			: m_buffer(src), m_strategy(std::move(strategy))
		{

		}
	protected:
		void Consume(T val)
		{
			m_strategy->Consume(val);
		}

		Concurrency::ISource<T>& m_buffer;
	private:
		std::unique_ptr<IAsyncConsumerStrategy<T>> m_strategy;
	};

	// create other versions of this if you need to customize any of Start, Stop, Wait or LastValue policies
	template<typename T>
	using StrategyBasedAsyncConsumer = AsyncConsumer<ConsumerWithStrategy<T>,
		Skills::AutoStart,
		Skills::AutoStop,
		Skills::AutoWait,
		Skills::RetainLastValues>;
}