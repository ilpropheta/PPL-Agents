# PPL Agents

This small repository contains a few abstractions I have built on top of Microsoft PPL's `<agents.h>` module.

Since the library itself is deprecated and Microsoft does not provide support for it anymore, I am adopting other libraries. In my opinion, it's a pity that Microsoft abandoned the development of this library and never invested to make it cross-platform. It covers some concurrent programming patterns and constructs very well (and simply).

On the other hand, its .NET core cousin [Dataflow](https://docs.microsoft.com/en-us/dotnet/standard/parallel-programming/dataflow-task-parallel-library) (previously known as *TPL - Task Parallel Library*) is widely supported and used. Consider also that Rene Stein has developed [this library](https://github.com/renestein/Rstein.AsyncCpp) inspired by TPL.

## What's in this repository

A few things, really. No rocket science.

### Bringing cancellation to agents

PPL's agents are simple to use. You just inherit from `Concurrency::agent` and override `agent::run` to customize the agent behavior. `run` is executed when the agent is scheduled for running (after you call `start` on it). However, agents do not provide *cancellation* as you might expect. They can be cancelled only *before* they start. 

In other words, you can create an agent and cancel it before it moves to the "running" state. However, many times you need to stop the agent *while it's running*.

Thus, this repository contains a wrapper on top of `Concurrency::agent` that provides a cancellation concept as you might expect and hides the other cancellation mechanism (if you need it, just add it to the code, I won't be offended).

You just inherit from `Agents::Agent` and override `Agent::Run(CancellationToken&)`:

```cpp
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
```

A few things to clarify:

- `CancellationToken` is implemented very simply in terms of `Concurrency::single_assignment`
- `agent::done` is called automatically when you return from `Run` (even if you throw an exception)
- the agent still needs to be started and stopped manually, however...keep on reading

An example:

```cpp
MyAgent a;
a.Start();
std::this_thread::sleep_for(2s);
a.StopAndWait();
```

### Declaring agents with skills

Sometimes you just want to start and stop your agents automatically (in a RAII fashion).

Here is why this repository contains an non-intrusive way to declare your agents with some additional skills (policies): the `AgentComposer`.

For example:

```cpp
using AutoStartAgent = AgentComposer<MyAgent, Skills::AutoStart>;

AutoStartAgent a; // starts automatically
std::this_thread::sleep_for(2s);
a.StopAndWait();
```

Another example:

```cpp
using RAIIAgent = AgentComposer<MyAgent, Skills::AutoStart, Skills::AutoWait, Skills::AutoStop>;

RAIIAgent a; // starts automatically
std::this_thread::sleep_for(2s);
// stops and waits automatically
```

As you notice, `Skills::AutoWait` has been placed before `Skills::AutoStop`. This is because of the order destructions: `Stop` has to be called before `Wait` otherwise you end up waitint undefinitely! Another way to declare that agent follows:

```cpp
using RAIIAgent = AgentComposer<MyAgent, Skills::AutoStart, Skills::AutoStopAndWait>;
```

As said, this is not rocker science and **could be improved a lot**. This stuff has been useful to me for some projects and I am just sharing it with the community before I totally forget about it.

Clearly, `AgentComposer` is not coupled with `Agent` at all. You could change its name and reuse this concept for other things if you need.

Also, you can create additional skills:

```cpp
template<typename T>
struct MySkill
{
	void Foo()
	{
		std::cout << "foo\n";
	}
};

using AnotherRAIIAgent = AgentComposer<MyAgent, Skills::AutoStart, Skills::AutoStopAndWait, MySkill>;
```

### Receive or stop

In several scenarios I faced a common pattern: given an async data source (e.g. `unbounded_buffer`), stay blocked consuming all the messages or stop as soon as it's requested.

The PPL provides a nice construct for that: `choice`. Basically, you can receive from multiple channels at the same time:

```cpp
auto received = false;
auto stop = false;

Concurrency::single_assignment<bool> stop;
Concurrency::unbounded_buffer<string> data;
// ... e.g. stop and data are passed to another thread ...

// your loop body
auto messageSource = Concurrency::make_choice(&stop, &data);
// block until either stop or message arrives
auto messageIdx = receive(messageSource);
// message received?
if (messageIdx != 1 && messageSource.has_value())
{
  std::cout << "Received: " << messageSource.value<string>() << "\n";
}
else if (messageIdx == 0)
{
  std::cout << "Stop received or \n";
}
```

The reason of the second check is because in some cases I have found that `messageSource.has_value() == false` even if `messageIdx == 1`. This can happen when you have multiple consumers on the same buffer.

Another important point is the order of arguments here `Concurrency::make_choice(&stop, &data);`. If you put `stop` after `data` then you could never terminate if `data` keeps on getting in data! This is like a "short-circuit" in `||`: "if the first one does not have data then I check the second one, and so on...".

This repo contains a wrapper on top of `Concurrency::receive` to support cancellation while receiving. The cancellation is managed by the same `CancellationToken` provided by `Agent::Run`:

```cpp
template<typename T>
bool Receive(Concurrency::ISource<T>& source, CancellationToken& cancellation, T& out, unsigned timeout = Concurrency::COOPERATIVE_TIMEOUT_INFINITE);
```

So we can write an agent this way:

```cpp
struct SimpleConsumer : Agent
{
	void SendMessage(int i)
	{
		send(m_data, i);
	}

protected:
	void Run(CancellationToken& token)
	{
		int value = 0;
		while (Receive(m_data, token, value))
		{
			std::cout << value << "\n";
		}
	}
private:
	Concurrency::unbounded_buffer<int> m_data;
};

// ...

AgentComposer<SimpleConsumer, AutoStart> agent;
agent.SendMessage(1);
agent.SendMessage(2);
agent.SendMessage(3);
// ... maybe after some time

agent.StopAndWait();
```

This was quite common for me in the past and I wrote a simple class encapsulating everything but the "Consume" function.

### Consume all the buffer or stop: AsyncConsumerAgent

`AsyncConsumerAgent` is a utility class that must be parametrized with a Consumer behavior and with a policy for the last messages that can be stay in queue after the stop command has been received.

The `Consumer` is expected to provide:

- `void Consume(T)`
- a member `m_buffer` compatible with the type `Concurrency::ISource<T>` and accessible from subclasses (e.g. `protected`)

Also, `Consumer` mustn't be `final`.

For example:

```cpp
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
protected:
	Concurrency::unbounded_buffer<int>& m_buffer;
};

//...

using AutoAllAsyncConsumer = AsyncConsumer<MyConsumer, AutoStart, AutoStop, AutoWait, RetainLastValues>;
		
Concurrency::unbounded_buffer<int> b;
AutoAllAsyncConsumer consumer{ b };

for(auto i=0; i<10; ++i)
{
  send(b, i);
}
```

`AsyncConsumer` is just a wrapper on top of `AgentComposer` and requires start, stop and wait skills. Additionally it requires a policy for processing the last values that might stay in the buffer *after* the cancellation has been requested.

`AsyncConsumer` also supports cancellation from inside the consumer:

- if `Consume` throws a `StopFromInsideConsumerException` exception, `m_buffer` is linked to a sort of "null buffer".
- if `Consume` throws any other `std::exception` exception.

In both cases, the `Agent` underneath is moved to the `AgentStatus::Completed` state.

Just to spend a few words more about the first behavior, consider this example:

```cpp
struct MyThrowingConsumer
{
	MyThrowingConsumer(Concurrency::unbounded_buffer<int>& b)
		: m_buffer(b)
	{

	}

	void Consume(int i)
	{
		if (m_counter++ == 10)
		{
			throw std::runtime_error();
		}
		std::cout << "MyThrowingConsumer is handling: " << i << "\n";
	}

protected:
	Concurrency::unbounded_buffer<int>& m_buffer;
private:
	int m_counter = 0;
};

int main()
{
    	using AutoAllThrowingConsumer = AsyncConsumer<MyThrowingConsumer, AutoStart, AutoStop, AutoWait, RetainLastValues>;
		
    	Concurrency::unbounded_buffer<int> vals;
	Concurrency::unbounded_buffer<int> anotherBuffer;
	AutoAllThrowingConsumer consumer{ vals };

	for (auto i=0; i<100; ++i)
	{
		Concurrency::send(vals, i);
		Concurrency::send(anotherBuffer, i);
	}
	Concurrency::wait(300); // simulate some delay

	int val = 0;
	while (try_receive(vals, val))
	{
		std::cout << "this is still in the buffer: " << val << "\n";
	}
}
```

In this case, `vals` keeps on receiving values but nobody dequeue them! It could be a problem. Probably the best approach is to link buffers in advance and then unlink them when the agent is terminated, but sometimes you could not do this. So, `AsyncConsumer` uses a trick that consists in linking `m_buffer` with an internal `overwrite_buffer`:

```cpp
catch (const StopFromInsideConsumerException&)
{
  static Concurrency::overwrite_buffer<payloadType> NullBuffer;
  // since the consumer has failed and this Agent will be AgentStatus::Completed when returning to the caller,
  // we link its buffer to an overwrite_buffer that will behave like a "null consumer"
  buffer.link_target(&NullBuffer);
}
```

Basically, the normal behavior of `overwrite_buffer` is just to get messages and keep only the most recent one. This is like consuming all incoming messages if nobody receive them. When we `link` `m_buffer` to an `overwrite_buffer` we are basically moving all the messages to the latter.

I won't recommend this approach but this is supported.

The complete example:

```cpp
struct MyThrowingConsumer
{
	MyThrowingConsumer(Concurrency::unbounded_buffer<int>& b)
		: m_buffer(b)
	{

	}

	void Consume(int i)
	{
		if (m_counter++ == 10)
		{
			throw StopFromInsideConsumerException();
		}
		std::cout << "MyThrowingConsumer is handling: " << i << "\n";
	}

protected:
	Concurrency::unbounded_buffer<int>& m_buffer;
private:
	int m_counter = 0;
};

int main()
{
  using AutoAllThrowingConsumer = AsyncConsumer<MyThrowingConsumer, AutoStart, AutoStop, AutoWait, RetainLastValues>;
  Concurrency::unbounded_buffer<int> vals;
  Concurrency::unbounded_buffer<int> anotherBuffer;
  AutoAllThrowingConsumer consumer{ vals };

  for (auto i=0; i<100; ++i)
  {
    Concurrency::send(vals, i);
    Concurrency::send(anotherBuffer, i);
  }
  Concurrency::wait(300);

  int val = 0;
  while (try_receive(vals, val))
  {
    std::cout << "this is still in vals: " << val << "\n";
  }
  while (try_receive(anotherBuffer, val))
  {
    std::cout << "this is still in anotherBuffer: " << val << "\n";
  }
}
```

## External resources

- [(Book) Parallel Programming with Microsoft Visual C++](https://www.amazon.com/Parallel-Programming-Microsoft-Visual-Decomposition/dp/0735651752)
- [(Official doc) Asynchronous Agents Library](https://docs.microsoft.com/en-us/cpp/parallel/concrt/asynchronous-agents-library)
