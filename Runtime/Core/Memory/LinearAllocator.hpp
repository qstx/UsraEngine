#pragma once

#include <Core/Memory/Allocator.hpp>
#include <thread>

namespace Ursa::Memory
{
class LinearAllocator final : public Allocator
{
public:
	struct Marker
	{
		Size cursor = 0;
		UInt64 sequence = 0;
		UInt64 generation = 0;
	};

	explicit LinearAllocator(Size capacity = 1024 * 1024);

	~LinearAllocator() override;

	Marker Mark() const noexcept;

	void Restore(Marker marker) noexcept;

	void Reset() noexcept;

	Size Used() const noexcept;

	Size Capacity() const noexcept;

	UInt64 Generation() const noexcept;

	UInt64 OverflowCount() const noexcept;

private:
	RawAllocation AllocateRaw(Size size) override;

	void DeallocateRaw(void* pointer, Size size) noexcept override;

	void OnAllocated(Detail::AllocationHeader* header) noexcept override;

	void OnDeallocated(Detail::AllocationHeader* header) noexcept override;

	void CheckThread() const noexcept;

	void* storage = nullptr;
	Size capacity = 0;
	Size cursor = 0;
	UInt64 sequence = 0;
	UInt64 generation = 0;
	UInt64 overflowCount = 0;
	std::thread::id thread;
	Detail::AllocationHeader* head = nullptr;
};

LinearAllocator& ThreadTemporaryAllocator();

class TemporaryScope final
{
public:
	explicit TemporaryScope(LinearAllocator& allocator = ThreadTemporaryAllocator()) noexcept;

	~TemporaryScope();

	TemporaryScope(const TemporaryScope&) = delete;
	TemporaryScope& operator=(const TemporaryScope&) = delete;

	LinearAllocator& GetAllocator() const noexcept;

private:
	LinearAllocator& allocator;
	LinearAllocator::Marker marker;
};
} // namespace Ursa::Memory
