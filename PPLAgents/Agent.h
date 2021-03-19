#pragma once
#include <agents.h>
#include "Utils.h"

namespace Agents
{
	// simple cancellation token implemented in terms of Concurrency::single_assignment<bool>
	// can only be used to check if cancellation has been requested
	class CancellationToken
	{
	public:
		bool IsCancellationRequested()
		{
			return m_cancellationRequested || try_receive(m_target, m_cancellationRequested);
		}
	private:
		CancellationToken(Concurrency::single_assignment<bool>& target)
			: m_target(target)
		{

		}

		friend class CancellationTokenSource;

		template<typename T>
		friend bool Receive(Concurrency::ISource<T>& source, CancellationToken& cancellation, T& out, unsigned timeout);

		Concurrency::single_assignment<bool>& m_target;
		bool m_cancellationRequested = false;
	};

	// simple cancellation source that decouples cancellation tokens from the cancel operation
	// can be used to obtain multiple cancellation tokens that can be all cancelled by the same source
	// (useful to control a group of workers)
	class CancellationTokenSource
	{
	public:
		void Cancel()
		{
			asend(m_target, true);
		}

		CancellationToken Token()
		{
			return { m_target };
		}

		bool IsCancellationRequested()
		{
			return m_cancelled || Concurrency::try_receive(m_target, m_cancelled);
		}
	private:
		Concurrency::single_assignment<bool> m_target;
		bool m_cancelled = false;
	};

	// just like Concurrency::receive
	template<typename T>
	T Receive(Concurrency::ISource<T>& source, unsigned timeout)
	{
		return receive(source, timeout);
	}

	// just like Concurrency::receive
	template<typename T>
	T Receive(Concurrency::ISource<T>& source)
	{
		return receive(source);
	}

	// just like Concurrency::try_receive
	template<typename T>
	bool TryReceive(Concurrency::ISource<T>& source, T& out)
	{
		return try_receive(source, out);
	}

	// smart wrapper on top of Concurrency::receive that supports cancellation:
	// this function can return because of either:
	// - a message has been received from "source" to "out" [returns true]
	// - a cancellation has been requested on "cancellation" [returns false]
	// - "timeout" has expired [in this case the function lets Concurrency::receive throw an exception]
	//
	// This function can be used to implement the classical idiom:
	// "stay (cooperatively) blocked receiving data until a cancellation has been requested"
	template<typename T>
	bool Receive(Concurrency::ISource<T>& source, CancellationToken& cancellation, T& out, unsigned timeout = Concurrency::COOPERATIVE_TIMEOUT_INFINITE)
	{
		auto received = false;
		auto stop = false;
		while (!received && !stop)
		{
			auto messageSource = Concurrency::make_choice(&cancellation.m_target, &source);
			auto messageIdx = receive(messageSource, timeout);
			stop = messageIdx != 1;
			if (!stop && messageSource.has_value())
			{
				out = messageSource.template value<T>();
				received = true;
			}
		}
		return !stop;
	}
	
	enum class AgentStatus
	{
		Created,
		Runnable,
		Started,
		Completed,
		Stopped,
		Waited
	};

	// Just an abstraction on top of PPL's agent:
	// - hides Concurrency::agent (in case you need to replace it with something else)
	// - supports cancellation during execution (stop)
	// - it does not support Concurrency::agent::cancel (cancellation 'before' execution)
	class Agent : Concurrency::agent
	{
	public:
		void Start()
		{
			start();
			m_status = AgentStatus::Runnable;
		}
		void Stop()
		{
			m_tokenSource.Cancel();
			m_status = AgentStatus::Stopped;
		}
		void Wait()
		{
			wait(this);
			m_status = AgentStatus::Waited;
		}
		void StopAndWait()
		{
			Stop();
			Wait();
		}
		[[nodiscard]] AgentStatus Status() const
		{
			return m_status;
		}
	protected:
		virtual void Run(CancellationToken& cancellationToken) = 0;
	private:
		void run() override
		{
			m_status = AgentStatus::Started;
			Utils::defer doneGuard([this] { done(); m_status = AgentStatus::Completed; });
			auto token = m_tokenSource.Token();
			Run(token);
		}

		CancellationTokenSource m_tokenSource;
		AgentStatus m_status = AgentStatus::Created;
	};
}