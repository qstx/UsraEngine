#include <Core/Profiler.hpp>
#include <Core/Memory/Memory.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>

namespace Ursa::Profiling
{
namespace
{
#if defined(URSA_ENABLE_TRACY_MEMORY) || defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
struct CategoryNames
{
	std::array<char, 80> pool{};
	std::array<std::array<char, 112>, 6> plots{};
};

struct State
{
	std::array<CategoryNames, Memory::maxMemoryTags> categories{};
	std::mutex events;
	std::mutex sampling;
	std::chrono::steady_clock::time_point nextSample{};
};

State& GetState() noexcept
{
	// The allocator remains usable from static and thread-local destructors.
	alignas(State) static unsigned char storage[sizeof(State)];
	static State* state = new (storage) State;
	return *state;
}
#endif

#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
Size EventLength(const char* text, Size length) noexcept
{
	constexpr Size limit = 1024;
	Size count = length < limit ? length : limit;
	if (count < length)
	{
		while (count && (static_cast<unsigned char>(text[count]) & 0xC0) == 0x80)
		{
			--count;
		}
	}
	return count;
}
#endif
} // namespace

bool IsConnected() noexcept
{
#if defined(URSA_ENABLE_TRACY)
	return tracy::ProfilerAvailable() && TracyIsConnected;
#else
	return false;
#endif
}

Int32 MemoryCallstackDepth() noexcept
{
#if defined(URSA_ENABLE_TRACY_MEMORY)
	return URSA_TRACY_MEMORY_CALLSTACK_DEPTH;
#else
	return 0;
#endif
}

void MemoryAllocation(const void* pointer, Size size, const char* pool) noexcept
{
#if defined(URSA_ENABLE_TRACY_MEMORY)
	if (pointer && pool && IsConnected())
	{
		std::lock_guard lock(GetState().events);
		TracyAllocNS(pointer, size, URSA_TRACY_MEMORY_CALLSTACK_DEPTH, pool);
	}
#else
	(void)pointer;
	(void)size;
	(void)pool;
#endif
}

void MemoryRelease(const void* pointer, const char* pool) noexcept
{
#if defined(URSA_ENABLE_TRACY_MEMORY)
	if (pointer && pool && IsConnected())
	{
		std::lock_guard lock(GetState().events);
		TracyFreeNS(pointer, 0, pool);
	}
#else
	(void)pointer;
	(void)pool;
#endif
}

void Plot(const char* name, Float64 value, PlotUnit unit) noexcept
{
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	if (name && IsConnected())
	{
		const auto format = unit == PlotUnit::Bytes        ? tracy::PlotFormatType::Memory
		                    : unit == PlotUnit::Percentage ? tracy::PlotFormatType::Percentage
		                                                   : tracy::PlotFormatType::Number;
		TracyPlotConfig(name, format, true, false, 0);
		TracyPlot(name, value);
	}
#else
	(void)name;
	(void)value;
	(void)unit;
#endif
}

void Event(const char* text, Size length) noexcept
{
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	if (text && length && IsConnected())
	{
		TracyMessage(text, EventLength(text, length));
	}
#else
	(void)text;
	(void)length;
#endif
}

void Operation(const char* name, const char* path, const char* result) noexcept
{
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	if (!IsConnected())
	{
		return;
	}
	std::array<char, 2049> text{};
	std::snprintf(text.data(), text.size(), "%s | %s | %s", name ? name : "", path ? path : "", result ? result : "");
	// Keep enough lookahead for Event to truncate at a complete UTF-8 codepoint.
	Event(text.data(), std::strlen(text.data()));
#else
	(void)name;
	(void)path;
	(void)result;
#endif
}

void ApplicationInfo(const char* name) noexcept
{
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	std::array<char, 2049> text{};
#if defined(URSA_ENABLE_TRACY_MEMORY)
	constexpr bool memory = true;
#else
	constexpr bool memory = false;
#endif
	std::snprintf(text.data(), text.size(),
	              "%s | Ursa %s | revision %s | %s | Vulkan | memory=%d diagnostics=1 stack=%d", name ? name : "Ursa",
	              URSA_PROFILING_VERSION, URSA_PROFILING_REVISION, URSA_BUILD_CONFIGURATION, memory ? 1 : 0,
	              MemoryCallstackDepth());
	if (tracy::ProfilerAvailable())
	{
		TracyAppInfo(text.data(), EventLength(text.data(), std::strlen(text.data())));
	}
#else
	(void)name;
#endif
}

void SampleCpuMemory() noexcept
{
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	if (!IsConnected())
	{
		return;
	}
	auto& state = GetState();
	std::unique_lock lock(state.sampling, std::try_to_lock);
	const auto now = std::chrono::steady_clock::now();
	if (!lock.owns_lock() || now < state.nextSample)
	{
		return;
	}
	state.nextSample = now + std::chrono::milliseconds(250);
	const auto snapshot = Memory::Snapshot();
	for (Size index = 0; index < snapshot.categoryCount; ++index)
	{
		const auto& category = snapshot.categories[index];
		const auto& names = state.categories[index].plots;
		const UInt64 values[]{category.currentBytes,    category.peakBytes,    category.liveAllocations,
		                      category.allocationCount, category.releaseCount, category.overflowCount};
		for (Size metric = 0; metric < 6; ++metric)
		{
			Plot(names[metric].data(), static_cast<Float64>(values[metric]),
			     metric < 2 ? PlotUnit::Bytes : PlotUnit::Number);
		}
	}
	const auto heap = Memory::DefaultAllocator().Statistics();
	Plot("Ursa.Cpu.Heap.Reserved", static_cast<Float64>(heap.reservedBytes), PlotUnit::Bytes);
	Plot("Ursa.Cpu.Heap.UpstreamAllocations", static_cast<Float64>(heap.upstreamAllocations));
#endif
}

namespace Detail
{
void RegisterCpuCategory(Size index, const char* name) noexcept
{
#if defined(URSA_ENABLE_TRACY_MEMORY) || defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	if (index < Memory::maxMemoryTags && name)
	{
		auto& category = GetState().categories[index];
		std::snprintf(category.pool.data(), category.pool.size(), "Ursa.Cpu.%s", name);
		const char* suffix[]{"Current", "Peak", "Live", "Allocations", "Frees", "Overflows"};
		for (Size metric = 0; metric < 6; ++metric)
		{
			std::snprintf(category.plots[metric].data(), category.plots[metric].size(), "%s.%s", category.pool.data(),
			              suffix[metric]);
		}
	}
#else
	(void)index;
	(void)name;
#endif
}

const char* CpuPool(Size index) noexcept
{
#if defined(URSA_ENABLE_TRACY_MEMORY) || defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
	return index < Memory::maxMemoryTags ? GetState().categories[index].pool.data() : nullptr;
#else
	(void)index;
	return nullptr;
#endif
}
} // namespace Detail
} // namespace Ursa::Profiling
