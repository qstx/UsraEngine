#pragma once

#include <Core/Memory/MemoryTypes.hpp>
#include <cstddef>

namespace Ursa::Memory
{
namespace Detail
{
struct AllocationHeader;
}

class Allocator
{
public:
	virtual ~Allocator() = default;

	Allocator() = default;
	Allocator(const Allocator&) = delete;
	Allocator& operator=(const Allocator&) = delete;

	void* Allocate(Size size, Size alignment = alignof(std::max_align_t), MemoryTag tag = MemoryTag::Core,
	               Size alignmentOffset = 0);

	void* TryAllocate(Size size, Size alignment = alignof(std::max_align_t), MemoryTag tag = MemoryTag::Core,
	                  Size alignmentOffset = 0) noexcept;

	static void Deallocate(void* pointer) noexcept;

protected:
	struct RawAllocation
	{
		void* memory = nullptr;
		bool overflow = false;
	};

	virtual RawAllocation AllocateRaw(Size size) = 0;

	virtual void DeallocateRaw(void* pointer, Size size) noexcept = 0;

	virtual void OnAllocated(Detail::AllocationHeader*) noexcept
	{
	}

	virtual void OnDeallocated(Detail::AllocationHeader*) noexcept
	{
	}
};
} // namespace Ursa::Memory
