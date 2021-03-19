#pragma once
#include "Utils.h"

namespace Agents
{
	namespace Skills
	{
		template<typename T>
		struct AutoStop
		{
			~AutoStop()
			{
				static_cast<T&>(*this).Stop();
			}
		};

		template<typename T>
		struct AutoWait
		{
			~AutoWait()
			{
				static_cast<T&>(*this).Wait();
			}
		};

		template<typename T>
		struct AutoStopAndWait
		{
			~AutoStopAndWait()
			{
				static_cast<T&>(*this).StopAndWait();
			}
		};

		template<typename T>
		struct AutoStart
		{
			AutoStart()
			{
				static_cast<T&>(*this).Start();
			}
		};

		template<typename T>
		struct ManualStart { };

		template<typename T>
		struct ManualStop { };

		template<typename T>
		struct ManualWait { };

		// policy to process last values
		struct RetainLastValues
		{
			template<typename Buffer, typename Consumer>
			static void Process(Buffer& buffer, Consumer consumer)
			{
				decltype(Utils::detect(buffer)) received;
				while (try_receive(buffer, received))
				{
					consumer(received);
				}
			}
		};

		// policy to ignore last values
		struct DropLastValues
		{
			template<typename Buffer, typename Consumer>
			static void Process(Buffer&, Consumer&)
			{
			}
		};
	}

	template<typename Behavior, template<typename> typename... AgentSkills>
	struct AgentComposer : Behavior, AgentSkills<AgentComposer<Behavior, AgentSkills...>>...
	{
		using Behavior::Behavior;
	};
}