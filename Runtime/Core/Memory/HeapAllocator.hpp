#pragma once

#include <Core/Memory/Allocator.hpp>

namespace Ursa::Memory
{
class LinearAllocator;

class HeapAllocator final : public Allocator
{
public:
	HeapAllocator();

	~HeapAllocator() override;

	HeapStatistics Statistics() const noexcept;

	void ReleaseUnused() noexcept;

private:
	friend class LinearAllocator;
	struct Impl;
	Impl* impl = nullptr;

	RawAllocation AllocateRaw(Size size) override;

	void DeallocateRaw(void* pointer, Size size) noexcept override;
};
} // namespace Ursa::Memory
