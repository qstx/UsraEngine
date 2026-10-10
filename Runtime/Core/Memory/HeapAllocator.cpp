#include <Core/Memory/HeapAllocator.hpp>
#include <Core/Memory/MemoryLock.hpp>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <new>

namespace Ursa::Memory
{
namespace
{
constexpr Size pageBytes = 64 * 1024;
constexpr Size retainedPagesPerBucket = 2;
constexpr std::array<Size, 7> sizeClasses{128, 256, 512, 1024, 2048, 4096, 8192};

struct alignas(std::max_align_t) Page
{
	Page* next = nullptr;
	Size bucket = 0;
	Size freeSlots = 0;
	Size slotCount = 0;
};

struct alignas(std::max_align_t) Block
{
	Page* page = nullptr;
	Block* next = nullptr;
};

struct Bucket
{
	Detail::SpinMutex mutex;
	Page* pages = nullptr;
	Block* freeList = nullptr;
	Size freePages = 0;
	Size liveBlocks = 0;
};
} // namespace

struct HeapAllocator::Impl
{
	std::array<Bucket, sizeClasses.size()> buckets;
	std::atomic<UInt64> reservedBytes{0};
	std::atomic<UInt64> upstreamAllocations{0};
	std::atomic<UInt64> upstreamReleases{0};
	std::atomic<UInt64> liveLargeBlocks{0};

	void AddPage(Size index)
	{
		void* memory = std::malloc(pageBytes);
		if (!memory)
		{
			throw std::bad_alloc();
		}
		auto* page = new (memory) Page;
		page->bucket = index;
		page->slotCount = (pageBytes - sizeof(Page)) / sizeClasses[index];
		page->freeSlots = page->slotCount;
		auto& bucket = buckets[index];
		page->next = bucket.pages;
		bucket.pages = page;
		++bucket.freePages;
		for (Size slot = 0; slot < page->slotCount; ++slot)
		{
			auto* block = new (static_cast<unsigned char*>(memory) + sizeof(Page) + slot * sizeClasses[index]) Block;
			block->page = page;
			block->next = bucket.freeList;
			bucket.freeList = block;
		}
		reservedBytes.fetch_add(pageBytes, std::memory_order_relaxed);
		upstreamAllocations.fetch_add(1, std::memory_order_relaxed);
	}

	void RemovePage(Bucket& bucket, Page* page) noexcept
	{
		Block** cursor = &bucket.freeList;
		while (*cursor)
		{
			if ((*cursor)->page == page)
			{
				*cursor = (*cursor)->next;
			}
			else
			{
				cursor = &(*cursor)->next;
			}
		}
		Page** currentPage = &bucket.pages;
		while (*currentPage != page)
		{
			currentPage = &(*currentPage)->next;
		}
		*currentPage = page->next;
		--bucket.freePages;
		reservedBytes.fetch_sub(pageBytes, std::memory_order_relaxed);
		upstreamReleases.fetch_add(1, std::memory_order_relaxed);
		std::free(page);
	}
};

HeapAllocator::HeapAllocator()
{
	void* memory = ::operator new(sizeof(Impl), std::align_val_t(alignof(Impl)));
	impl = new (memory) Impl;
}

HeapAllocator::~HeapAllocator()
{
	assert(Statistics().liveBlocks == 0 && "Allocator must outlive its allocations");
	ReleaseUnused();
	impl->~Impl();
	::operator delete(impl, std::align_val_t(alignof(Impl)));
}

Allocator::RawAllocation HeapAllocator::AllocateRaw(Size size)
{
	if (size > std::numeric_limits<Size>::max() - sizeof(Block))
	{
		throw std::bad_alloc();
	}
	const Size bytes = size + sizeof(Block);
	Size index = 0;
	while (index < sizeClasses.size() && bytes > sizeClasses[index])
	{
		++index;
	}
	Block* block;
	if (index == sizeClasses.size())
	{
		void* memory = std::malloc(bytes);
		if (!memory)
		{
			throw std::bad_alloc();
		}
		block = new (memory) Block;
		impl->reservedBytes.fetch_add(bytes, std::memory_order_relaxed);
		impl->upstreamAllocations.fetch_add(1, std::memory_order_relaxed);
		impl->liveLargeBlocks.fetch_add(1, std::memory_order_relaxed);
	}
	else
	{
		auto& bucket = impl->buckets[index];
		std::lock_guard<Detail::SpinMutex> lock(bucket.mutex);
		if (!bucket.freeList)
		{
			impl->AddPage(index);
		}
		block = bucket.freeList;
		bucket.freeList = block->next;
		if (block->page->freeSlots == block->page->slotCount)
		{
			--bucket.freePages;
		}
		--block->page->freeSlots;
		++bucket.liveBlocks;
	}
	return {block + 1, false};
}

void HeapAllocator::DeallocateRaw(void* pointer, Size size) noexcept
{
	auto* block = static_cast<Block*>(pointer) - 1;
	if (!block->page)
	{
		impl->reservedBytes.fetch_sub(size + sizeof(Block), std::memory_order_relaxed);
		impl->upstreamReleases.fetch_add(1, std::memory_order_relaxed);
		impl->liveLargeBlocks.fetch_sub(1, std::memory_order_relaxed);
		std::free(block);
	}
	else
	{
		Page* page = block->page;
		auto& bucket = impl->buckets[page->bucket];
		std::lock_guard<Detail::SpinMutex> lock(bucket.mutex);
		block->next = bucket.freeList;
		bucket.freeList = block;
		++page->freeSlots;
		--bucket.liveBlocks;
		if (page->freeSlots == page->slotCount)
		{
			++bucket.freePages;
			if (bucket.freePages > retainedPagesPerBucket)
			{
				impl->RemovePage(bucket, page);
			}
		}
	}
}

HeapStatistics HeapAllocator::Statistics() const noexcept
{
	UInt64 liveBlocks = impl->liveLargeBlocks.load(std::memory_order_relaxed);
	for (auto& bucket : impl->buckets)
	{
		std::lock_guard<Detail::SpinMutex> lock(bucket.mutex);
		liveBlocks += bucket.liveBlocks;
	}
	return {impl->reservedBytes.load(std::memory_order_relaxed),
	        impl->upstreamAllocations.load(std::memory_order_relaxed),
	        impl->upstreamReleases.load(std::memory_order_relaxed), liveBlocks};
}

void HeapAllocator::ReleaseUnused() noexcept
{
	for (auto& bucket : impl->buckets)
	{
		std::lock_guard<Detail::SpinMutex> lock(bucket.mutex);
		Page* page = bucket.pages;
		while (page)
		{
			Page* next = page->next;
			if (page->freeSlots == page->slotCount)
			{
				impl->RemovePage(bucket, page);
			}
			page = next;
		}
	}
}
} // namespace Ursa::Memory
