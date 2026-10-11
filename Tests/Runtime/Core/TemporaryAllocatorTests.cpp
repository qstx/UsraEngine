#include <Core/Memory/LinearAllocator.hpp>
#include <Core/Memory/Memory.hpp>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

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

void TestScopes()
{
	LinearAllocator allocator(4096);
	const auto tag = RegisterTag("TemporaryTests");
	const auto before = Snapshot().categories[static_cast<Size>(tag)];
	void* parent = allocator.Allocate(32, 32, tag);
	const Size parentUsed = allocator.Used();
	{
		TemporaryScope scope(allocator);
		void* aligned = allocator.Allocate(128, 256, tag);
		Expect(reinterpret_cast<UIntPtr>(aligned) % 256 == 0, "Temporary alignment failed");
		{
			TemporaryScope nested(allocator);
			Free(parent);
			allocator.Allocate(10000, 64, tag);
			Expect(allocator.OverflowCount() == 1, "Overflow was not recorded");
		}
		Expect(Inspect(aligned).size == 128, "Nested restore released outer memory");
		Free(aligned);
	}
	Expect(allocator.Used() == parentUsed, "Scope cursor was not restored");
	const auto generation = allocator.Generation();
	allocator.Reset();
	Expect(allocator.Used() == 0 && allocator.Generation() == generation + 1, "Full reset failed");
	const auto after = Snapshot().categories[static_cast<Size>(tag)];
	Expect(after.currentBytes == before.currentBytes && after.liveAllocations == before.liveAllocations,
	       "Temporary reset leaked logical allocations");
	Expect(after.overflowCount == before.overflowCount + 1, "Category overflow count mismatch");
	const auto heapBefore = DefaultAllocator().Statistics();
	for (int frame = 0; frame < 1000; ++frame)
	{
		TemporaryScope scope(allocator);
		for (int index = 0; index < 10; ++index)
		{
			allocator.Allocate(64, 16, tag);
		}
	}
	Expect(DefaultAllocator().Statistics().upstreamAllocations == heapBefore.upstreamAllocations,
	       "Warmed temporary scope called upstream heap");
}

void TestThreadIsolation()
{
	const auto tag = RegisterTag("ThreadTemporaryTests");
	std::atomic<bool> valid{true};
	std::vector<std::thread> workers;
	for (int index = 0; index < 4; ++index)
	{
		workers.emplace_back(
		    [tag, &valid]
		    {
			    auto& allocator = ThreadTemporaryAllocator();
			    for (int frame = 0; frame < 100; ++frame)
			    {
				    TemporaryScope scope;
				    auto* block = static_cast<UInt64*>(allocator.Allocate(sizeof(UInt64), alignof(UInt64), tag));
				    *block = 12345;
				    if (*block != 12345)
				    {
					    valid = false;
				    }
			    }
			    if (allocator.Used() != 0)
			    {
				    valid = false;
			    }
		    });
	}
	for (auto& worker : workers)
	{
		worker.join();
	}
	Expect(valid, "Thread temporary state was corrupted");
	Expect(Snapshot().categories[static_cast<Size>(tag)].liveAllocations == 0, "Thread exit leaked allocations");
}
} // namespace

int main()
{
	try
	{
		TestScopes();
		TestThreadIsolation();
		std::cout << "Temporary allocator tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
