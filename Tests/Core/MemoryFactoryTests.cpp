#include <Core/Memory/MemoryFactories.hpp>
#include <Core/Memory/MemoryContainers.hpp>
#include <Core/Memory/MemoryAlgorithms.hpp>
#include <algorithm>
#include <Core/Containers.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
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

struct Probe
{
	int* destroyed;

	explicit Probe(int& count, bool fail = false) : destroyed(&count)
	{
		if (fail)
		{
			throw std::runtime_error("Expected constructor failure");
		}
	}

	~Probe()
	{
		++*destroyed;
	}
};

struct alignas(128) AlignedProbe
{
	std::array<UInt8, 128> data{};
};

void TestFactories()
{
	HeapAllocator owner;
	const auto tag = RegisterTag("CustomTypeTests");
	const auto baseline = Snapshot().categories[static_cast<Size>(tag)];
	int destroyed = 0;
	{
		auto object = MakeUnique<Probe>(owner, tag, destroyed);
		auto moved = std::move(object);
		Expect(!object && Inspect(moved.get()).tag == tag, "Factory ownership/tag was lost");
		moved.reset(New<Probe>(owner, tag, destroyed));
		Expect(destroyed == 1, "Unique reset did not destroy old object");
	}
	Expect(destroyed == 2, "Unique destructor count mismatch");
	try
	{
		New<Probe>(owner, tag, destroyed, true);
		throw std::logic_error("Throwing constructor did not fail");
	}
	catch (const std::runtime_error&)
	{
	}
	try
	{
		MakeShared<Probe>(owner, tag, destroyed, true);
		throw std::logic_error("Throwing shared constructor did not fail");
	}
	catch (const std::runtime_error&)
	{
	}
	Expect(destroyed == 2 && owner.Statistics().liveBlocks == 0, "Construction failure leaked storage");
	{
		auto unique = MakeUnique<AlignedProbe>(owner, tag);
		auto shared = MakeShared<AlignedProbe>(owner, tag);
		Expect(reinterpret_cast<UIntPtr>(unique.get()) % 128 == 0 && reinterpret_cast<UIntPtr>(shared.get()) % 128 == 0,
		       "Factory over-alignment failed");
		std::weak_ptr<AlignedProbe> weak = shared;
		shared.reset();
		Expect(weak.expired(), "Shared instance stayed alive");
		weak.reset();
	}
	const auto after = Snapshot().categories[static_cast<Size>(tag)];
	Expect(after.currentBytes == baseline.currentBytes && after.liveAllocations == baseline.liveAllocations,
	       "Factories did not restore category baseline");
}

void TestContainers()
{
	const auto tag = RegisterTag("MemoryContainerTests");
	HeapAllocator allocator;
	{
		MemoryVector<int> values(ContainerAllocator(allocator, tag));
		for (int index = 0; index < 4096; ++index)
		{
			values.push_back(index);
		}
		Expect(values.back() == 4095 && Inspect(values.data()).tag == tag, "EASTL container category/value failed");
		MemoryMap<int, int> map{eastl::less<int>{}, ContainerAllocator(allocator, tag)};
		map[1] = 42;
		Expect(map[1] == 42, "Allocator-aware map failed");
		std::vector<int, StlAllocator<int>> standard{StlAllocator<int>(allocator, tag)};
		standard.resize(128, 42);
		Expect(Inspect(standard.data()).tag == tag, "Standard adapter lost category");
	}
	Expect(allocator.Statistics().liveBlocks == 0, "Container storage leaked");
	LinearAllocator temporary;
	{
		TemporaryScope scope(temporary);
		TemporaryVector<int> scratch{TemporaryContainerAllocator(temporary, tag)};
		scratch.resize(128, 3);
		Expect(scratch.back() == 3, "Temporary container value failed");
	}
	Expect(temporary.Used() == 0 && Snapshot().categories[static_cast<Size>(tag)].liveAllocations == 0,
	       "Temporary container outlived scope");
	const auto containersBefore = Snapshot().categories[static_cast<Size>(MemoryTag::Containers)];
	{
		Vector<int> defaultValues;
		defaultValues.resize(512);
		Expect(Inspect(defaultValues.data()).tag == MemoryTag::Containers, "Default EASTL was not routed");
	}
	const auto containersAfter = Snapshot().categories[static_cast<Size>(MemoryTag::Containers)];
	Expect(containersAfter.currentBytes == containersBefore.currentBytes &&
	           containersAfter.liveAllocations == containersBefore.liveAllocations,
	       "Default EASTL leaked");
}

void TestSortCleanup()
{
	std::array<int, 8> values{8, 7, 6, 5, 4, 3, 2, 1};
	const Size used = ThreadTemporaryAllocator().Used();
	const auto before = Snapshot().categories[static_cast<Size>(MemoryTag::Temporary)];
	try
	{
		StableSort(values.begin(), values.end(),
		           [](int, int) -> bool { throw std::runtime_error("Expected comparator failure"); });
	}
	catch (const std::runtime_error&)
	{
	}
	Expect(ThreadTemporaryAllocator().Used() == used &&
	           Snapshot().categories[static_cast<Size>(MemoryTag::Temporary)].liveAllocations == before.liveAllocations,
	       "Throwing comparator leaked scratch storage");
	StableSort(values.begin(), values.end(), std::less<int>{});
	Expect(std::is_sorted(values.begin(), values.end()), "Allocator-aware stable sort failed");
	for (Size count : {Size(0), Size(1), Size(2), Size(16), Size(17), Size(64), Size(257)})
	{
		std::vector<std::pair<int, Size>> entries;
		for (Size index = 0; index < count; ++index)
		{
			entries.emplace_back(static_cast<int>(index * 37 % 13), index);
		}
		auto expected = entries;
		const auto compare = [](const auto& left, const auto& right) { return left.first < right.first; };
		std::stable_sort(expected.begin(), expected.end(), compare);
		StableSort(entries.begin(), entries.end(), compare);
		Expect(entries == expected, "Stable sort changed equivalent-key ordering or range boundaries");
	}
}
} // namespace

int main()
{
	try
	{
		TestFactories();
		TestContainers();
		TestSortCleanup();
		std::cout << "Memory factory/container tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
