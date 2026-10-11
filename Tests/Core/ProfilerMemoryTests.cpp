#include <Core/Profiler.hpp>
#include <Core/Containers.hpp>
#include <Core/Log.hpp>
#include <Core/Memory/Memory.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Core/Memory/LinearAllocator.hpp>
#include <Rhi/RHI.hpp>
#include <Vfs/AssetRoots.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
using namespace Ursa;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> result)
{
	Expect(static_cast<bool>(result), result.error.c_str());
	return std::move(result.value);
}

struct LifetimeProbe
{
	void* block = Memory::Allocate(13);

	~LifetimeProbe()
	{
		Memory::Free(block);
	}
};

LifetimeProbe staticProbe;

void CheckCpuMemory()
{
	URSA_PROFILE_SCOPE("ProfilerTest.CpuMemory");
	const auto tag = Memory::RegisterTag("ProfilerTest");
	const auto baseline = Memory::Snapshot().categories[static_cast<Size>(tag)];
	{
		Memory::HeapAllocator heap;
		for (Size size : {Size(0), Size(1), Size(37), Size(4096), Size(9000)})
		{
			void* pointer = heap.Allocate(size, 64, tag);
			Expect(reinterpret_cast<UIntPtr>(pointer) % 64 == 0, "Profiling changed allocation alignment");
			std::thread releaser([pointer] { Memory::Free(pointer); });
			releaser.join();
		}
		Expect(!heap.TryAllocate(1, 3, tag), "Invalid alignment accepted");
		Expect(!heap.TryAllocate(std::numeric_limits<Size>::max(), 64, tag), "Overflow allocation accepted");
		Memory::LinearAllocator linear(256);
		void* outer = linear.Allocate(8, 8, tag);
		{
			Memory::TemporaryScope inner(linear);
			linear.Allocate(512, 8, tag);
			void* released = linear.Allocate(5, 8, tag);
			Memory::Free(released);
		}
		Expect(Memory::Inspect(outer).size == 8, "Temporary scope discarded an outer allocation");
		linear.Reset();
		auto object = MakeUnique<Int32>(heap, tag, 42);
		Expect(*object == 42, "Object factory changed its result");
		Vector<Int32> container(16, 7);
		Expect(container.back() == 7, "EASTL allocation changed its result");
	}
	std::array<std::thread, 4> workers;
	for (auto& worker : workers)
	{
		worker = std::thread(
		    [tag]
		    {
			    thread_local LifetimeProbe threadProbe;
			    Memory::LinearAllocator linear(1024);
			    for (Size index = 0; index < 128; ++index)
			    {
				    Memory::Free(Memory::Allocate(24, 8, tag));
				    Memory::TemporaryScope scope(linear);
				    linear.Allocate(64, 8, tag);
			    }
		    });
	}
	for (auto& worker : workers)
	{
		worker.join();
	}
	const auto after = Memory::Snapshot().categories[static_cast<Size>(tag)];
	Expect(after.currentBytes == baseline.currentBytes && after.liveAllocations == baseline.liveAllocations,
	       "Profiling workload did not return to its logical baseline");
	Expect(after.allocationCount - baseline.allocationCount == after.releaseCount - baseline.releaseCount,
	       "Profiling workload allocation/release counts differ");
}

void CheckGpuMemory()
{
	URSA_PROFILE_SCOPE("ProfilerTest.GpuMemory");
	// Device-local opaque handles can collide. Pool identity must disambiguate them.
#if defined(URSA_ENABLE_TRACY_MEMORY)
	static constexpr char firstDevicePool[] = "Ursa.Test.GpuDeviceA";
	static constexpr char secondDevicePool[] = "Ursa.Test.GpuDeviceB";
	URSA_PROFILE_MEMORY_ALLOC(reinterpret_cast<const void*>(UIntPtr(0x410)), 17, firstDevicePool);
	URSA_PROFILE_MEMORY_ALLOC(reinterpret_cast<const void*>(UIntPtr(0x410)), 29, secondDevicePool);
	URSA_PROFILE_MEMORY_FREE(reinterpret_cast<const void*>(UIntPtr(0x410)), firstDevicePool);
	URSA_PROFILE_MEMORY_FREE(reinterpret_cast<const void*>(UIntPtr(0x410)), secondDevicePool);
#endif
	std::array<Rhi::DevicePtr, 2> devices;
	for (auto& device : devices)
	{
		device = Take(Rhi::CreateDevice({Rhi::Backend::Vulkan, {}, true}));
		Vector<Rhi::BufferPtr> buffers;
		for (Size index = 0; index < 4; ++index)
		{
			buffers.push_back(Take(device->CreateBuffer({1024 * 1024, Rhi::BufferUsage::Storage, Rhi::Memory::Gpu})));
		}
		auto command = Take(device->CreateCommandList());
		Expect(static_cast<bool>(command->Begin()), "Cannot begin profiling test command");
		Expect(static_cast<bool>(command->End()), "Cannot end profiling test command");
		auto fence = Take(device->Submit(command));
		Expect(static_cast<bool>(fence->Wait()), "Cannot wait for profiling test command");
		Expect(static_cast<bool>(device->WaitIdle()), "GPU profiling emitted Vulkan validation errors");
	}
	devices = {};
}

void CheckLocks()
{
	ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>());
	std::array<std::thread, 2> workers;
	for (auto& worker : workers)
	{
		worker = std::thread(
		    []
		    {
			    for (Size index = 0; index < 128; ++index)
			    {
				    auto subscription = Log::Subscribe([](const LogRecord&) {});
				    Expect(static_cast<bool>(OpenEngineFileSystem()), "Concurrent engine mount read failed");
				    Log::Unsubscribe(subscription);
			    }
		    });
	}
	for (auto& worker : workers)
	{
		worker.join();
	}
	ConfigureEngineFileSystem({});
}

#if defined(_MSC_VER)
__declspec(noinline)
#endif
void* AllocateFromFirstCallsite(Memory::MemoryTag tag)
{
	return Memory::Allocate(41, 8, tag);
}

#if defined(_MSC_VER)
__declspec(noinline)
#endif
void* AllocateFromSecondCallsite(Memory::MemoryTag tag)
{
	return Memory::Allocate(73, 8, tag);
}

void CaptureScenario(UInt32 seconds, bool gpu)
{
	const auto tag = Memory::RegisterTag("ProfilerSession");
	void* beforeConnection = Memory::Allocate(123, 8, tag);
	void* spanningDisconnect = nullptr;
	bool wasConnected = false;
	bool sampledGpu = false;
	UInt32 connections = 0;
	const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
	while (std::chrono::steady_clock::now() < end)
	{
		const bool connected = Profiling::IsConnected();
		if (connected && !wasConnected)
		{
			++connections;
			Memory::Free(beforeConnection);
			beforeConnection = nullptr;
			spanningDisconnect = Memory::Allocate(321, 8, tag);
			CheckCpuMemory();
			CheckLocks();
			if (gpu && !sampledGpu)
			{
				CheckGpuMemory();
				sampledGpu = true;
			}
			URSA_PROFILE_OPERATION("ProfilerTest.Session", "", "connected");
#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
			String longText;
			for (Size index = 0; index < 400; ++index)
			{
				longText += "资源";
			}
			URSA_PROFILE_EVENT(longText.data(), longText.size());
			URSA_PROFILE_OPERATION("ProfilerTest.Operation", longText.c_str(), "cancelled");
#endif
		}
		if (!connected && wasConnected)
		{
			Memory::Free(spanningDisconnect);
			spanningDisconnect = nullptr;
			beforeConnection = Memory::Allocate(123, 8, tag);
		}
		if (connected)
		{
			Memory::Free(AllocateFromFirstCallsite(tag));
			Memory::Free(AllocateFromSecondCallsite(tag));
		}
		wasConnected = connected;
		URSA_PROFILE_FRAME("ProfilerTest");
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	Memory::Free(beforeConnection);
	Memory::Free(spanningDisconnect);
	std::cout << "capture-connections=" << connections << '\n';
	Expect(connections >= 1, "Capture scenario never connected to Tracy");
}
} // namespace

int main(int argc, char** argv)
{
	try
	{
		URSA_PROFILE_APP("UrsaProfilerMemoryTests");
		UInt32 seconds = 0;
		bool gpu = false;
		for (int index = 1; index < argc; ++index)
		{
			if (std::strcmp(argv[index], "--gpu") == 0)
			{
				gpu = true;
			}
			else if (std::strcmp(argv[index], "--seconds") == 0 && index + 1 < argc)
			{
				seconds = static_cast<UInt32>(std::strtoul(argv[++index], nullptr, 10));
			}
			else
			{
				throw std::runtime_error("Unknown profiling test argument");
			}
		}
		if (seconds)
		{
			CaptureScenario(seconds, gpu);
		}
		else
		{
			CheckCpuMemory();
			CheckLocks();
			if (gpu)
			{
				CheckGpuMemory();
			}
		}
		std::cout << "Profiling memory semantics passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
