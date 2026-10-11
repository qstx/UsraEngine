#include <Core/Profiler.hpp>
#include <Core/Containers.hpp>
#include <thread>
#include <stdexcept>

int main()
{
	Ursa::String message = "Profiler facade compile test";
	URSA_PROFILE_FRAME("Ursa Profiler Compile Test");
	URSA_PROFILE_SCOPE("Profiler Facade Compile Test");
	URSA_PROFILE_SCOPE_DYNAMIC(message.data(), message.size());
	URSA_PROFILE_MESSAGE(message);
	int plotEffects = 0;
	URSA_PROFILE_PLOT("Ursa.Test.Value", ++plotEffects, Ursa::Profiling::PlotUnit::Number);
	URSA_PROFILE_EVENT(message.data(), message.size());
	URSA_PROFILE_APP("UrsaProfilerCompileTests");
	int memoryEffects = 0;
	URSA_PROFILE_MEMORY_ALLOC(nullptr, ++memoryEffects, "Ursa.Test.Memory");
	URSA_PROFILE_MEMORY_FREE(nullptr, "Ursa.Test.Memory");
	URSA_PROFILE_LOCK(std::mutex, mutex, "Ursa.Test.Lock");
	try
	{
		std::lock_guard lock(mutex);
		throw std::runtime_error("Verify lock release during exception unwinding");
	}
	catch (const std::runtime_error&)
	{
	}
	int count = 0;
	auto increment = [&]
	{
		for (int index = 0; index < 1000; ++index)
		{
			std::lock_guard lock(mutex);
			++count;
		}
	};
	std::thread worker(increment);
	increment();
	worker.join();
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	constexpr int expectedPlots = 1;
#else
	constexpr int expectedPlots = 0;
#endif
#if defined(URSA_ENABLE_TRACY_MEMORY)
	constexpr int expectedMemory = 1;
#else
	constexpr int expectedMemory = 0;
#endif
	if (count != 2000 || plotEffects != expectedPlots || memoryEffects != expectedMemory)
	{
		return 1;
	}
	return 0;
}
