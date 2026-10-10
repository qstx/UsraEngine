#include <Core/Memory/LinearAllocator.hpp>
#include <Core/Memory/Memory.hpp>
#include <Core/Memory/MemoryInternal.hpp>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>

namespace Ursa::Memory
{
LinearAllocator::LinearAllocator(Size requestedCapacity)
    : capacity(requestedCapacity), thread(std::this_thread::get_id())
{
	if (capacity)
	{
		storage = std::malloc(capacity);
		if (!storage)
		{
			throw std::bad_alloc();
		}
	}
}

LinearAllocator::~LinearAllocator()
{
	Reset();
	std::free(storage);
}

void LinearAllocator::CheckThread() const noexcept
{
	assert(thread == std::this_thread::get_id() && "Temporary allocator is confined to its owning thread");
}

Allocator::RawAllocation LinearAllocator::AllocateRaw(Size size)
{
	if (thread != std::this_thread::get_id())
	{
		throw std::logic_error("Temporary allocation attempted from the wrong thread");
	}
	const Size alignment = alignof(std::max_align_t);
	if (cursor <= std::numeric_limits<Size>::max() - (alignment - 1))
	{
		const Size alignedCursor = (cursor + alignment - 1) & ~(alignment - 1);
		if (alignedCursor <= capacity && size <= capacity - alignedCursor)
		{
			cursor = alignedCursor + size;
			return {static_cast<unsigned char*>(storage) + alignedCursor, false};
		}
	}
	auto allocation = DefaultAllocator().AllocateRaw(size);
	allocation.overflow = true;
	++overflowCount;
	return allocation;
}

void LinearAllocator::DeallocateRaw(void* pointer, Size size) noexcept
{
	CheckThread();
	const auto address = reinterpret_cast<UIntPtr>(pointer);
	const auto begin = reinterpret_cast<UIntPtr>(storage);
	if (!storage || address < begin || address - begin >= capacity)
	{
		DefaultAllocator().DeallocateRaw(pointer, size);
	}
}

void LinearAllocator::OnAllocated(Detail::AllocationHeader* header) noexcept
{
	header->sequence = ++sequence;
	header->previous = nullptr;
	header->next = head;
	if (head)
	{
		head->previous = header;
	}
	head = header;
}

void LinearAllocator::OnDeallocated(Detail::AllocationHeader* header) noexcept
{
	CheckThread();
	if (header->previous)
	{
		header->previous->next = header->next;
	}
	else
	{
		head = header->next;
	}
	if (header->next)
	{
		header->next->previous = header->previous;
	}
}

LinearAllocator::Marker LinearAllocator::Mark() const noexcept
{
	CheckThread();
	return {cursor, sequence, generation};
}

void LinearAllocator::Restore(Marker marker) noexcept
{
	CheckThread();
	assert(marker.generation == generation && marker.cursor <= cursor && marker.sequence <= sequence);
	while (head && head->sequence > marker.sequence)
	{
		Free(head->user);
	}
	cursor = marker.cursor;
}

void LinearAllocator::Reset() noexcept
{
	CheckThread();
	while (head)
	{
		Free(head->user);
	}
	cursor = 0;
	++generation;
}

Size LinearAllocator::Used() const noexcept
{
	CheckThread();
	return cursor;
}

Size LinearAllocator::Capacity() const noexcept
{
	return capacity;
}

UInt64 LinearAllocator::Generation() const noexcept
{
	return generation;
}

UInt64 LinearAllocator::OverflowCount() const noexcept
{
	return overflowCount;
}

LinearAllocator& ThreadTemporaryAllocator()
{
	static thread_local LinearAllocator allocator;
	return allocator;
}

TemporaryScope::TemporaryScope(LinearAllocator& selectedAllocator) noexcept
    : allocator(selectedAllocator), marker(allocator.Mark())
{
}

TemporaryScope::~TemporaryScope()
{
	allocator.Restore(marker);
}

LinearAllocator& TemporaryScope::GetAllocator() const noexcept
{
	return allocator;
}
} // namespace Ursa::Memory
