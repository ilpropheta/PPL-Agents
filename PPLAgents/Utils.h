#pragma once

namespace Agents::Utils
{
	// very simple "defer" (finally) concept
	template <class F>
	class defer : F
	{
	public:
		explicit defer(F f) noexcept
			: F(std::move(f)) {}

		defer(defer&&) = delete;
		defer(const defer&) = delete;
		defer& operator=(const defer&) = delete;
		defer& operator=(defer&&) = delete;

		~defer() noexcept
		{
			F::operator()();
		}
	};

	template<typename T>
	T detect(Concurrency::ISource<T>&);	
}