#include <Core/Memory/Memory.hpp>
#include <Core/Memory/LinearAllocator.hpp>
#include <Core/Memory/MemoryAlgorithms.hpp>
#include <Core/Profiler.hpp>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <malloc.h>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <vector>
#include <thread>
#include <cstring>

namespace
{
using namespace Ursa;
using namespace Ursa::Memory;
volatile UInt64 checksum = 0;
constexpr Size iterations = 2000;
constexpr Size batch = 128;

class TrackedCrtAllocator final : public Allocator
{
private:
	RawAllocation AllocateRaw(Size size) override
	{
		return {_aligned_malloc(size, alignof(std::max_align_t)), false};
	}

	void DeallocateRaw(void* pointer, Size) noexcept override
	{
		_aligned_free(pointer);
	}
};

struct SortEntry
{
	int key = 0;
	int ordinal = 0;

	bool operator==(const SortEntry& other) const
	{
		return key == other.key && ordinal == other.ordinal;
	}
};

void BenchmarkSorting()
{
	std::array<SortEntry, 256> source;
	for (Size index = 0; index < source.size(); ++index)
	{
		source[index] = {static_cast<int>(index * 37 % 31), static_cast<int>(index)};
	}
	const auto compare = [](const SortEntry& left, const SortEntry& right) { return left.key < right.key; };
	auto expected = source;
	std::stable_sort(expected.begin(), expected.end(), compare);
	const auto run = [&](int policy)
	{
		const auto start = std::chrono::steady_clock::now();
		for (int frame = 0; frame < 2000; ++frame)
		{
			auto entries = source;
			if (policy == 1)
			{
				StableSort(entries.begin(), entries.end(), compare, MemoryTag::Renderer);
			}
			else if (policy == 2)
			{
				std::sort(entries.begin(), entries.end(), [](const SortEntry& left, const SortEntry& right)
				          { return left.key == right.key ? left.ordinal < right.ordinal : left.key < right.key; });
			}
			else
			{
				std::stable_sort(entries.begin(), entries.end(), compare);
			}
			if (entries != expected)
			{
				throw std::runtime_error("Allocator-aware sort changed stable ordering");
			}
			checksum += entries.back().ordinal;
		}
		return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 2000;
	};
	run(1);
	const auto before = DefaultAllocator().Statistics();
	const double baseline = run(0);
	const double adapted = run(1);
	const double ordinalSort = run(2);
	std::cout << "Renderer.DrawOrder: std-stable=" << baseline << " us/frame, ordinal-sort=" << ordinalSort
	          << " us/frame, Ursa-merge=" << adapted << " us/frame, warm-upstream="
	          << DefaultAllocator().Statistics().upstreamAllocations - before.upstreamAllocations << '\n';
	if (DefaultAllocator().Statistics().upstreamAllocations != before.upstreamAllocations)
	{
		throw std::runtime_error("Sort scratch called upstream allocator");
	}
}

void BenchmarkCommandBuffer()
{
	const std::wstring command(4096, L'x');
	const auto run = [&](bool temporary)
	{
		const auto start = std::chrono::steady_clock::now();
		for (int invocation = 0; invocation < 20000; ++invocation)
		{
			if (temporary)
			{
				TemporaryScope scope;
				std::vector<wchar_t, StlAllocator<wchar_t>> buffer(
				    command.begin(), command.end(), StlAllocator<wchar_t>(scope.GetAllocator(), MemoryTag::Editor));
				buffer.push_back(L'\0');
				checksum += buffer[command.size() - 1];
			}
			else
			{
				std::vector<wchar_t> buffer(command.begin(), command.end());
				buffer.push_back(L'\0');
				checksum += buffer[command.size() - 1];
			}
		}
		return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 20000;
	};
	run(true);
	const auto before = DefaultAllocator().Statistics();
	const double baseline = run(false);
	const double adapted = run(true);
	std::cout << "Editor.ProcessCommandBuffer: std=" << baseline << " ns/buffer, Ursa=" << adapted
	          << " ns/buffer, warm-upstream="
	          << DefaultAllocator().Statistics().upstreamAllocations - before.upstreamAllocations << '\n';
	if (DefaultAllocator().Statistics().upstreamAllocations != before.upstreamAllocations)
	{
		throw std::runtime_error("Command buffer scratch called upstream allocator");
	}
}

template <class Allocate, class Release> double Run(Allocate allocate, Release release)
{
	std::array<void*, batch> pointers{};
	const auto started = std::chrono::steady_clock::now();
	for (Size iteration = 0; iteration < iterations; ++iteration)
	{
		for (Size index = 0; index < batch; ++index)
		{
			const Size size = Size(24) << (index % 6);
			pointers[index] = allocate(size);
			if (!pointers[index])
			{
				throw std::bad_alloc();
			}
			static_cast<UInt8*>(pointers[index])[0] = static_cast<UInt8>(index);
		}
		for (Size index = 0; index < batch; ++index)
		{
			checksum += static_cast<UInt8*>(pointers[index])[0];
			release(pointers[index]);
		}
	}
	return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count() /
	       static_cast<double>(iterations * batch);
}
} // namespace

int Benchmark()
{
	HeapAllocator heap;
	const MemoryTag tag = RegisterTag("MemoryBenchmark");
	const double baseline = Run([](Size size) { return _aligned_malloc(size, alignof(std::max_align_t)); },
	                            [](void* pointer) { _aligned_free(pointer); });
	Run([&](Size size) { return heap.Allocate(size, alignof(std::max_align_t), tag); }, Free);
	const auto before = heap.Statistics();
	const double pooled = Run([&](Size size) { return heap.Allocate(size, alignof(std::max_align_t), tag); }, Free);
	const auto after = heap.Statistics();
	TrackedCrtAllocator trackedCrt;
	const double tracked =
	    Run([&](Size size) { return trackedCrt.Allocate(size, alignof(std::max_align_t), tag); }, Free);
	std::cout << "persistent-small: CRT=" << baseline << " ns/pair, Ursa=" << pooled
	          << " ns/pair, CRT-with-same-statistics=" << tracked
	          << " ns/pair, baseline-upstream=" << iterations * batch
	          << ", Ursa-warm-upstream=" << after.upstreamAllocations - before.upstreamAllocations << '\n';
	if (after.upstreamAllocations != before.upstreamAllocations || after.liveBlocks != 0)
	{
		return 1;
	}
	heap.ReleaseUnused();
	LinearAllocator temporary(128 * 1024);
	Size released = 0;
	const auto tempBefore = DefaultAllocator().Statistics();
	const double linear = Run([&](Size size) { return temporary.Allocate(size, alignof(std::max_align_t), tag); },
	                          [&](void* pointer)
	                          {
		                          Free(pointer);
		                          if (++released % batch == 0)
		                          {
			                          temporary.Reset();
		                          }
	                          });
	std::cout << "temporary-small: CRT=" << baseline << " ns/pair, Ursa=" << linear << " ns/pair, upstream="
	          << DefaultAllocator().Statistics().upstreamAllocations - tempBefore.upstreamAllocations
	          << ", overflows=" << temporary.OverflowCount() << '\n';
	if (temporary.OverflowCount() ||
	    DefaultAllocator().Statistics().upstreamAllocations != tempBefore.upstreamAllocations)
	{
		return 2;
	}
	BenchmarkSorting();
	BenchmarkCommandBuffer();
	return 0;
}

int main(int argc, char** argv)
{
	bool waitForProfiler = false;
	Ursa::UInt32 repeats = 1;
	for (int index = 1; index < argc; ++index)
	{
		if (std::strcmp(argv[index], "--wait-for-profiler") == 0)
		{
			waitForProfiler = true;
		}
		else if (std::strcmp(argv[index], "--repeat") == 0 && index + 1 < argc)
		{
			repeats = static_cast<Ursa::UInt32>(std::strtoul(argv[++index], nullptr, 10));
			if (!repeats || repeats > 20)
			{
				return 2;
			}
		}
		else
		{
			return 2;
		}
	}
	if (waitForProfiler)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		while (!Ursa::Profiling::IsConnected())
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return 3;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
	for (Ursa::UInt32 iteration = 0; iteration < repeats; ++iteration)
	{
		const int result = Benchmark();
		if (result)
		{
			return result;
		}
	}
	return 0;
}
