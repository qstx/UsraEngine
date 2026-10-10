#include <Core/Memory/Memory.hpp>
#include <atomic>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Memory;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void TestAlignmentAndOwnership()
{
	HeapAllocator owner;
	const auto tag = RegisterTag("MemoryAllocatorTests");
	const auto baseline = Snapshot().categories[static_cast<Size>(tag)];
	for (Size alignment : {Size(1), Size(8), Size(64), Size(4096)})
	{
		void* block = owner.Allocate(37, alignment, tag);
		Expect(reinterpret_cast<UIntPtr>(block) % alignment == 0, "Alignment was lost");
		std::memset(block, 0xA5, 37);
		Expect(Inspect(block).size == 37 && Inspect(block).tag == tag, "Allocation metadata was lost");
		std::thread releaser([block] { Free(block); });
		releaser.join();
	}
	void* offset = owner.Allocate(128, 64, tag, 8);
	Expect((reinterpret_cast<UIntPtr>(offset) + 8) % 64 == 0, "Offset alignment was lost");
	Free(offset);
	Expect(!owner.TryAllocate(10, 0, tag) && !owner.TryAllocate(10, 3, tag), "Invalid alignment accepted");
	Expect(!owner.TryAllocate(std::numeric_limits<Size>::max(), 16, tag), "Overflow size accepted");
	Expect(!owner.TryAllocate(1, 16, static_cast<MemoryTag>(maxMemoryTags)), "Invalid tag accepted");
	Free(owner.Allocate(0, 1, tag));
	Free(nullptr);
	const auto after = Snapshot().categories[static_cast<Size>(tag)];
	Expect(after.currentBytes == baseline.currentBytes && after.liveAllocations == baseline.liveAllocations,
	       "Release did not return usage to baseline");
	Expect(after.allocationCount - baseline.allocationCount == 6 && after.releaseCount - baseline.releaseCount == 6,
	       "Allocation/release counts mismatch");
	Expect(after.peakBytes >= 128, "Peak usage was not recorded");
	Expect(owner.Statistics().liveBlocks == 0, "Wrong allocator released a block");
}

class ExhaustedAllocator final : public Allocator
{
private:
	RawAllocation AllocateRaw(Size) override
	{
		return {};
	}

	void DeallocateRaw(void*, Size) noexcept override
	{
	}
};

void TestAllocationFailure()
{
	ExhaustedAllocator allocator;
	Expect(!allocator.TryAllocate(64), "Exhausted allocator did not return null");
	bool threw = false;
	try
	{
		allocator.Allocate(64);
	}
	catch (const std::bad_alloc&)
	{
		threw = true;
	}
	Expect(threw, "Exhausted allocator did not throw bad_alloc");
}

void TestSnapshots()
{
	const auto tag = RegisterTag("ConcurrentMemoryTests");
	const auto before = Snapshot().categories[static_cast<Size>(tag)];
	std::atomic<bool> running{true};
	std::atomic<bool> valid{true};
	std::thread observer(
	    [&]
	    {
		    while (running.load())
		    {
			    const auto state = Snapshot().categories[static_cast<Size>(tag)];
			    if (state.currentBytes != state.liveAllocations * 32 ||
			        state.allocationCount - state.releaseCount != state.liveAllocations ||
			        state.peakBytes < state.currentBytes)
			    {
				    valid = false;
			    }
		    }
	    });
	std::vector<std::thread> workers;
	for (int worker = 0; worker < 4; ++worker)
	{
		workers.emplace_back(
		    [tag]
		    {
			    for (int index = 0; index < 2000; ++index)
			    {
				    Free(Allocate(32, 16, tag));
			    }
		    });
	}
	for (auto& worker : workers)
	{
		worker.join();
	}
	running = false;
	observer.join();
	Expect(valid, "Concurrent snapshot was inconsistent");
	const auto after = Snapshot().categories[static_cast<Size>(tag)];
	Expect(after.currentBytes == before.currentBytes && after.liveAllocations == before.liveAllocations,
	       "Concurrent allocation leaked");
	Expect(after.allocationCount - before.allocationCount == 8000, "Concurrent count mismatch");
}

void TestHeapReuse()
{
	HeapAllocator heap;
	void* first = heap.Allocate(64);
	Free(first);
	const auto warmed = heap.Statistics();
	for (int index = 0; index < 20000; ++index)
	{
		Free(heap.Allocate(64));
	}
	Expect(heap.Statistics().upstreamAllocations == warmed.upstreamAllocations,
	       "Small allocations did not reuse pages");
	void* large = heap.Allocate(1024 * 1024, 4096);
	Expect(reinterpret_cast<UIntPtr>(large) % 4096 == 0, "Large alignment failed");
	Free(large);
	std::vector<void*> burst;
	for (int index = 0; index < 6000; ++index)
	{
		burst.push_back(heap.Allocate(64));
	}
	for (void* pointer : burst)
	{
		Free(pointer);
	}
	Expect(heap.Statistics().reservedBytes <= 2 * 64 * 1024, "Empty pages were not bounded");
	heap.ReleaseUnused();
	const auto clean = heap.Statistics();
	Expect(clean.reservedBytes == 0 && clean.upstreamAllocations == clean.upstreamReleases,
	       "Unused pages were not released");
}
} // namespace

int main()
{
	try
	{
		TestAlignmentAndOwnership();
		TestAllocationFailure();
		TestSnapshots();
		TestHeapReuse();
		std::cout << "Memory allocator correctness tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
