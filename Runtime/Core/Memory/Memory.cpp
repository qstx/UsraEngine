#include <Core/Memory/Memory.hpp>
#include <Core/Memory/MemoryInternal.hpp>
#include <Core/Memory/MemoryLock.hpp>
#include <Core/Profiler.hpp>
#include <atomic>
#include <cassert>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>

namespace Ursa::Memory
{
namespace Detail
{
struct Counters
{
	UInt64 currentBytes = 0;
	UInt64 liveAllocations = 0;
	UInt64 allocationCount = 0;
	UInt64 releaseCount = 0;
	UInt64 overflowCount = 0;
};

struct StatisticsShard
{
	SpinMutex mutex;
	std::array<Counters, maxMemoryTags> categories{};
};
} // namespace Detail

namespace
{
constexpr UInt64 allocatedMagic = 0x555253414D454D31ull;
constexpr Size shardCount = 64;

struct Category
{
	std::array<char, 64> name{};
	std::atomic<UInt64> current{0};
	std::atomic<UInt64> peak{0};
};

struct State
{
	std::mutex tagMutex;
	std::array<Category, maxMemoryTags> categories{};
	std::array<Detail::StatisticsShard, shardCount> shards{};
	std::atomic<Size> categoryCount{static_cast<Size>(MemoryTag::Count)};
	std::atomic<Size> nextShard{0};

	State()
	{
		const char* names[]{"Core", "Containers", "Scene", "Assets", "Renderer", "Editor", "Temporary"};
		for (Size index = 0; index < static_cast<Size>(MemoryTag::Count); ++index)
		{
			std::memcpy(categories[index].name.data(), names[index], std::strlen(names[index]) + 1);
			URSA_PROFILE_CATEGORY(index, names[index]);
		}
	}
};

State& GetState()
{
	// Process lifetime storage also serves allocations from static destructors.
	alignas(State) static unsigned char storage[sizeof(State)];
	static State* state = new (storage) State;
	return *state;
}

Detail::StatisticsShard& ThreadShard()
{
	State& state = GetState();
	static thread_local Size index = state.nextShard.fetch_add(1, std::memory_order_relaxed) % shardCount;
	return state.shards[index];
}

Detail::AllocationHeader* Header(const void* pointer) noexcept
{
	Detail::AllocationHeader* header = nullptr;
	std::memcpy(&header, static_cast<const unsigned char*>(pointer) - sizeof(header), sizeof(header));
	assert(header && header->magic == allocatedMagic);
	return header;
}

void RecordAllocation(Detail::AllocationHeader& header, bool overflow)
{
	State& state = GetState();
	const Size index = static_cast<Size>(header.tag);
	header.shard = &ThreadShard();
	std::lock_guard<Detail::SpinMutex> lock(header.shard->mutex);
	auto& counters = header.shard->categories[index];
	counters.currentBytes += header.size;
	++counters.liveAllocations;
	++counters.allocationCount;
	counters.overflowCount += overflow ? 1 : 0;
	auto& category = state.categories[index];
	const UInt64 current = category.current.fetch_add(header.size, std::memory_order_relaxed) + header.size;
	UInt64 peak = category.peak.load(std::memory_order_relaxed);
	while (peak < current && !category.peak.compare_exchange_weak(peak, current, std::memory_order_relaxed))
	{
	}
}

void RecordRelease(const Detail::AllocationHeader& header) noexcept
{
	const Size index = static_cast<Size>(header.tag);
	std::lock_guard<Detail::SpinMutex> lock(header.shard->mutex);
	auto& counters = header.shard->categories[index];
	assert(counters.currentBytes >= header.size && counters.liveAllocations > 0);
	counters.currentBytes -= header.size;
	--counters.liveAllocations;
	++counters.releaseCount;
	GetState().categories[index].current.fetch_sub(header.size, std::memory_order_relaxed);
}
} // namespace

void* Allocator::Allocate(Size size, Size alignment, MemoryTag tag, Size alignmentOffset)
{
	if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
	    static_cast<Size>(tag) >= GetState().categoryCount.load(std::memory_order_acquire))
	{
		throw std::invalid_argument("Invalid memory alignment or category");
	}
	const Size prefix = sizeof(Detail::AllocationHeader) + sizeof(Detail::AllocationHeader*);
	const Size bytes = size ? size : 1;
	if (alignment - 1 > std::numeric_limits<Size>::max() - prefix ||
	    bytes > std::numeric_limits<Size>::max() - prefix - (alignment - 1))
	{
		throw std::bad_alloc();
	}
	const Size rawSize = prefix + (alignment - 1) + bytes;
	const RawAllocation raw = AllocateRaw(rawSize);
	if (!raw.memory)
	{
		throw std::bad_alloc();
	}
	auto* header = new (raw.memory) Detail::AllocationHeader;
	header->owner = this;
	header->size = size;
	header->rawSize = rawSize;
	header->tag = tag;
	header->magic = allocatedMagic;
	const UIntPtr candidate = reinterpret_cast<UIntPtr>(raw.memory) + prefix;
	const UIntPtr padding = (UIntPtr(0) - (candidate + alignmentOffset)) & (alignment - 1);
	header->user = reinterpret_cast<void*>(candidate + padding);
	std::memcpy(static_cast<unsigned char*>(header->user) - sizeof(header), &header, sizeof(header));
	RecordAllocation(*header, raw.overflow);
	OnAllocated(header);
	URSA_PROFILE_MEMORY_ALLOC(header->user, header->size, Profiling::Detail::CpuPool(static_cast<Size>(header->tag)));
	return header->user;
}

void* Allocator::TryAllocate(Size size, Size alignment, MemoryTag tag, Size alignmentOffset) noexcept
{
	try
	{
		return Allocate(size, alignment, tag, alignmentOffset);
	}
	catch (...)
	{
		return nullptr;
	}
}

void Allocator::Deallocate(void* pointer) noexcept
{
	if (!pointer)
	{
		return;
	}
	auto* header = Header(pointer);
	Allocator* owner = header->owner;
	const Size rawSize = header->rawSize;
	URSA_PROFILE_MEMORY_FREE(pointer, Profiling::Detail::CpuPool(static_cast<Size>(header->tag)));
	owner->OnDeallocated(header);
	RecordRelease(*header);
	header->magic = 0;
	owner->DeallocateRaw(header, rawSize);
}

HeapAllocator& DefaultAllocator()
{
	alignas(HeapAllocator) static unsigned char storage[sizeof(HeapAllocator)];
	static HeapAllocator* allocator = new (storage) HeapAllocator;
	return *allocator;
}

void* Allocate(Size size, Size alignment, MemoryTag tag, Size alignmentOffset)
{
	return DefaultAllocator().Allocate(size, alignment, tag, alignmentOffset);
}

void* TryAllocate(Size size, Size alignment, MemoryTag tag, Size alignmentOffset) noexcept
{
	try
	{
		return DefaultAllocator().TryAllocate(size, alignment, tag, alignmentOffset);
	}
	catch (...)
	{
		return nullptr;
	}
}

void Free(void* pointer) noexcept
{
	Allocator::Deallocate(pointer);
}

AllocationInfo Inspect(const void* pointer) noexcept
{
	if (!pointer)
	{
		return {};
	}
	const auto* header = Header(pointer);
	return {header->size, header->tag};
}

MemoryTag RegisterTag(const char* name)
{
	if (!name || !*name || std::strlen(name) >= 64)
	{
		throw std::invalid_argument("Memory category name must contain 1 to 63 bytes");
	}
	auto& state = GetState();
	std::lock_guard<std::mutex> lock(state.tagMutex);
	const Size count = state.categoryCount.load(std::memory_order_relaxed);
	for (Size index = 0; index < count; ++index)
	{
		if (std::strcmp(state.categories[index].name.data(), name) == 0)
		{
			return static_cast<MemoryTag>(index);
		}
	}
	if (count == maxMemoryTags)
	{
		throw std::length_error("Memory category limit reached");
	}
	std::memcpy(state.categories[count].name.data(), name, std::strlen(name) + 1);
	URSA_PROFILE_CATEGORY(count, name);
	state.categoryCount.store(count + 1, std::memory_order_release);
	return static_cast<MemoryTag>(count);
}

MemorySnapshot Snapshot()
{
	auto& state = GetState();
	std::array<std::unique_lock<Detail::SpinMutex>, shardCount> locks;
	for (Size index = 0; index < shardCount; ++index)
	{
		locks[index] = std::unique_lock<Detail::SpinMutex>(state.shards[index].mutex);
	}
	std::lock_guard<std::mutex> tagLock(state.tagMutex);
	MemorySnapshot result;
	result.categoryCount = state.categoryCount.load(std::memory_order_acquire);
	for (Size index = 0; index < result.categoryCount; ++index)
	{
		auto& category = result.categories[index];
		category.tag = static_cast<MemoryTag>(index);
		category.name = state.categories[index].name;
		category.peakBytes = state.categories[index].peak.load(std::memory_order_relaxed);
		for (const auto& shard : state.shards)
		{
			const auto& counters = shard.categories[index];
			category.currentBytes += counters.currentBytes;
			category.liveAllocations += counters.liveAllocations;
			category.allocationCount += counters.allocationCount;
			category.releaseCount += counters.releaseCount;
			category.overflowCount += counters.overflowCount;
		}
	}
	return result;
}

void ReleaseUnused() noexcept
{
	DefaultAllocator().ReleaseUnused();
}
} // namespace Ursa::Memory
