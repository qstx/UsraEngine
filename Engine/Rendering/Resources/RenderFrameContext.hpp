#pragma once

#include <Rhi/RHI.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <thread>

namespace Ursa::Rendering
{
struct UploadSlice
{
	Rhi::BufferPtr buffer;
	Size offset = 0;
	Size size = 0;
};

struct RenderFrameStatistics
{
	UInt64 frames = 0;
	UInt64 slotReuses = 0;
	UInt64 slotWaits = 0;
	UInt64 uploadedBytes = 0;
	UInt64 uploadBuffersCreated = 0;
	UInt64 uploadCapacity = 0;
};

class RenderFrameContext final
{
public:
	Rhi::CommandListPtr Command() const
	{
		return command;
	}

	const Rhi::AcquiredImage& Image() const
	{
		return image;
	}

	UInt64 Serial() const
	{
		return serial;
	}

	UInt32 Slot() const
	{
		return slot;
	}

private:
	friend class RenderFrameQueue;

	struct UploadBlock
	{
		Rhi::BufferPtr buffer;
		Size used = 0;
	};

	Rhi::CommandListPtr command;
	Rhi::FencePtr completion;
	Rhi::SwapchainPtr swapchain;
	Rhi::AcquiredImage image;
	Vector<UploadBlock> uploads;
	Vector<std::shared_ptr<void>> retained;
	UInt64 serial = 0;
	UInt32 slot = 0;
	Size uploadCapacity = 0;
};

class RenderFrameQueue final
{
public:
	static Result<UniquePtr<RenderFrameQueue>> Create(Rhi::DevicePtr device, Size uploadBlockBytes = 1024 * 1024,
	                                                  Size maxUploadBytesPerSlot = 64 * 1024 * 1024);

	~RenderFrameQueue();

	Result<RenderFrameContext*> Begin(const Rhi::SwapchainPtr& swapchain = {});

	Result<UploadSlice> Upload(const void* data, Size size, Size alignment);

	Result<void> Retain(std::shared_ptr<void> resource);

	Result<Rhi::FencePtr> Complete();

	Result<void> Abort();

	Result<void> Drain();

	const RenderFrameStatistics& Statistics() const
	{
		return statistics;
	}

private:
	RenderFrameQueue(Rhi::DevicePtr device, Size uploadBlockBytes, Size maxUploadBytesPerSlot);
	friend struct Ursa::MemoryDeleter<RenderFrameQueue>;
	template <class T, class... Args> friend T* Ursa::New(Memory::Allocator&, Memory::MemoryTag, Args&&...);

	Result<void> RequireThread() const;

	Rhi::DevicePtr device;
	std::thread::id ownerThread;
	Array<RenderFrameContext, 2> slots;
	RenderFrameContext* active = nullptr;
	UInt32 nextSlot = 0;
	UInt64 nextSerial = 1;
	Size uploadBlockBytes;
	Size maxUploadBytesPerSlot;
	RenderFrameStatistics statistics;
};
} // namespace Ursa::Rendering
