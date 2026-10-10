#pragma once

#include <Core/Types.hpp>
#include <array>

namespace Ursa::Memory
{
enum class MemoryTag : UInt16
{
	Core,
	Containers,
	Scene,
	Assets,
	Renderer,
	Editor,
	Temporary,
	Count
};

inline constexpr Size maxMemoryTags = 128;

struct CategoryStatistics
{
	MemoryTag tag = MemoryTag::Core;
	std::array<char, 64> name{};
	UInt64 currentBytes = 0;
	UInt64 peakBytes = 0;
	UInt64 liveAllocations = 0;
	UInt64 allocationCount = 0;
	UInt64 releaseCount = 0;
	UInt64 overflowCount = 0;
};

struct MemorySnapshot
{
	Size categoryCount = 0;
	std::array<CategoryStatistics, maxMemoryTags> categories{};
};

struct HeapStatistics
{
	UInt64 reservedBytes = 0;
	UInt64 upstreamAllocations = 0;
	UInt64 upstreamReleases = 0;
	UInt64 liveBlocks = 0;
};

struct AllocationInfo
{
	Size size = 0;
	MemoryTag tag = MemoryTag::Core;
};
} // namespace Ursa::Memory
