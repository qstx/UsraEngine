#pragma once

#include <Core/Memory/Allocator.hpp>

namespace Ursa::Memory::Detail
{
struct StatisticsShard;

struct alignas(std::max_align_t) AllocationHeader
{
	Allocator* owner = nullptr;
	StatisticsShard* shard = nullptr;
	AllocationHeader* previous = nullptr;
	AllocationHeader* next = nullptr;
	void* user = nullptr;
	Size size = 0;
	Size rawSize = 0;
	UInt64 sequence = 0;
	UInt64 magic = 0;
	MemoryTag tag = MemoryTag::Core;
};
} // namespace Ursa::Memory::Detail
