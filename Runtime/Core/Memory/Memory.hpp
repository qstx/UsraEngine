#pragma once

#include <Core/Memory/HeapAllocator.hpp>

namespace Ursa::Memory
{
HeapAllocator& DefaultAllocator();

void* Allocate(Size size, Size alignment = alignof(std::max_align_t), MemoryTag tag = MemoryTag::Core,
               Size alignmentOffset = 0);

void* TryAllocate(Size size, Size alignment = alignof(std::max_align_t), MemoryTag tag = MemoryTag::Core,
                  Size alignmentOffset = 0) noexcept;

void Free(void* pointer) noexcept;

AllocationInfo Inspect(const void* pointer) noexcept;

MemoryTag RegisterTag(const char* name);

MemorySnapshot Snapshot();

void ReleaseUnused() noexcept;
} // namespace Ursa::Memory
