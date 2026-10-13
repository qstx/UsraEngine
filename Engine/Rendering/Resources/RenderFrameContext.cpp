#include <Rendering/Resources/RenderFrameContext.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Core/Profiler.hpp>
#include <algorithm>
#include <limits>

namespace Ursa::Rendering
{
RenderFrameQueue::RenderFrameQueue(Rhi::DevicePtr selectedDevice, Size blockBytes, Size maximumBytes)
    : device(std::move(selectedDevice)), ownerThread(std::this_thread::get_id()), uploadBlockBytes(blockBytes),
      maxUploadBytesPerSlot(maximumBytes)
{
	slots[0].slot = 0;
	slots[1].slot = 1;
}

Result<UniquePtr<RenderFrameQueue>> RenderFrameQueue::Create(Rhi::DevicePtr device, Size uploadBlockBytes,
                                                             Size maxUploadBytesPerSlot)
{
	if (!device || !uploadBlockBytes || uploadBlockBytes > maxUploadBytesPerSlot ||
	    maxUploadBytesPerSlot > 256 * 1024 * 1024)
	{
		return Result<UniquePtr<RenderFrameQueue>>::Failure(
		    "Render frame queue needs a device and a bounded upload budget");
	}
	return Result<UniquePtr<RenderFrameQueue>>::Success(MakeUnique<RenderFrameQueue>(
	    Memory::MemoryTag::Renderer, std::move(device), uploadBlockBytes, maxUploadBytesPerSlot));
}

RenderFrameQueue::~RenderFrameQueue()
{
	Drain();
}

Result<void> RenderFrameQueue::RequireThread() const
{
	return std::this_thread::get_id() == ownerThread
	           ? Result<void>{}
	           : Result<void>::Failure("Render frame queue requires its owner thread");
}

Result<RenderFrameContext*> RenderFrameQueue::Begin(const Rhi::SwapchainPtr& swapchain)
{
	auto valid = RequireThread();
	if (!valid || active || nextSerial == 0)
	{
		return Result<RenderFrameContext*>::Failure(
		    valid ? "Previous render frame is active or serial space is exhausted" : valid.error);
	}
	auto& slot = slots[nextSlot];
	if (slot.completion)
	{
		++statistics.slotReuses;
		if (!slot.completion->Completed())
		{
			URSA_PROFILE_SCOPE("Rendering.FrameSlotWait");
			valid = slot.completion->Wait();
			if (!valid)
			{
				return Result<RenderFrameContext*>::Failure(valid.error);
			}
			++statistics.slotWaits;
		}
		slot.completion.reset();
	}
	slot.retained.clear();
	for (auto& upload : slot.uploads)
	{
		upload.used = 0;
	}
	slot.image = {};
	slot.swapchain.reset();
	if (slot.command)
	{
		valid = slot.command->Reset();
		if (!valid)
		{
			return Result<RenderFrameContext*>::Failure(valid.error);
		}
	}
	else
	{
		auto created = device->CreateCommandList();
		if (!created)
		{
			return Result<RenderFrameContext*>::Failure(created.error);
		}
		slot.command = std::move(created.value);
	}
	valid = slot.command->Begin();
	if (!valid)
	{
		return Result<RenderFrameContext*>::Failure(valid.error);
	}
	if (swapchain)
	{
		URSA_PROFILE_SCOPE("Rendering.SwapchainAcquire");
		auto image = swapchain->Acquire();
		if (!image)
		{
			slot.command->Reset();
			return Result<RenderFrameContext*>::Failure(image.error);
		}
		slot.image = std::move(image.value);
		slot.swapchain = swapchain;
	}
	slot.serial = nextSerial++;
	active = &slot;
	++statistics.frames;
	return Result<RenderFrameContext*>::Success(active);
}

Result<UploadSlice> RenderFrameQueue::Upload(const void* data, Size size, Size alignment)
{
	auto valid = RequireThread();
	if (!valid || !active || !data || !size || !alignment || (alignment & (alignment - 1)) ||
	    size > maxUploadBytesPerSlot)
	{
		return Result<UploadSlice>::Failure(
		    valid ? "Frame upload needs active storage, data, size and power-of-two alignment" : valid.error);
	}
	for (auto& block : active->uploads)
	{
		if (block.used > (std::numeric_limits<Size>::max)() - (alignment - 1))
		{
			continue;
		}
		const Size offset = (block.used + alignment - 1) & ~(alignment - 1);
		if (offset > block.buffer->desc.size || size > block.buffer->desc.size - offset)
		{
			continue;
		}
		valid = block.buffer->Write(offset, data, size);
		if (!valid)
		{
			return Result<UploadSlice>::Failure(valid.error);
		}
		block.used = offset + size;
		statistics.uploadedBytes += size;
		return Result<UploadSlice>::Success({block.buffer, offset, size});
	}
	const Size capacity = std::max(uploadBlockBytes, size);
	if (capacity > maxUploadBytesPerSlot - active->uploadCapacity)
	{
		return Result<UploadSlice>::Failure("Render frame upload budget exceeded");
	}
	auto created = device->CreateBuffer(
	    {capacity, Rhi::BufferUsage::Uniform | Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource,
	     Rhi::Memory::Upload});
	if (!created)
	{
		return Result<UploadSlice>::Failure(created.error);
	}
	valid = created.value->Write(0, data, size);
	if (!valid)
	{
		return Result<UploadSlice>::Failure(valid.error);
	}
	active->uploads.push_back({created.value, size});
	active->uploadCapacity += capacity;
	statistics.uploadCapacity += capacity;
	++statistics.uploadBuffersCreated;
	statistics.uploadedBytes += size;
	return Result<UploadSlice>::Success({std::move(created.value), 0, size});
}

Result<void> RenderFrameQueue::Retain(std::shared_ptr<void> resource)
{
	auto valid = RequireThread();
	if (!valid)
	{
		return valid;
	}
	if (!active || !resource)
	{
		return Result<void>::Failure("Frame retention requires an active frame and resource");
	}
	active->retained.push_back(std::move(resource));
	return {};
}

Result<Rhi::FencePtr> RenderFrameQueue::Complete()
{
	auto valid = RequireThread();
	if (!valid || !active)
	{
		return Result<Rhi::FencePtr>::Failure(valid ? "No active render frame" : valid.error);
	}
	valid = active->command->End();
	if (!valid)
	{
		return Result<Rhi::FencePtr>::Failure(valid.error);
	}
	auto submitted = device->Submit(active->command, active->image.texture ? active->swapchain : Rhi::SwapchainPtr{});
	if (!submitted)
	{
		return Result<Rhi::FencePtr>::Failure(submitted.error);
	}
	active->completion = submitted.value;
	const auto swapchain = active->image.texture ? active->swapchain : Rhi::SwapchainPtr{};
	active = nullptr;
	nextSlot = (nextSlot + 1) % UInt32(slots.size());
	if (swapchain)
	{
		URSA_PROFILE_SCOPE("Rendering.SwapchainPresent");
		valid = swapchain->Present();
		if (!valid)
		{
			return Result<Rhi::FencePtr>::Failure(valid.error);
		}
	}
	return submitted;
}

Result<void> RenderFrameQueue::Abort()
{
	auto valid = RequireThread();
	if (!valid)
	{
		return valid;
	}
	if (!active)
	{
		return {};
	}
	active->command->EndRendering();
	valid = active->command->Reset();
	if (!valid)
	{
		return valid;
	}
	if (active->image.texture)
	{
		valid = active->command->Begin();
		if (valid)
		{
			valid = active->command->Barrier(active->image.texture, active->image.initialState,
			                                 Rhi::ResourceState::Present);
		}
		if (!valid)
		{
			return valid;
		}
		auto completed = Complete();
		return completed ? Result<void>{} : Result<void>::Failure(completed.error);
	}
	active->retained.clear();
	active = nullptr;
	return {};
}

Result<void> RenderFrameQueue::Drain()
{
	auto valid = RequireThread();
	if (!valid)
	{
		return valid;
	}
	valid = Abort();
	if (!valid)
	{
		return valid;
	}
	for (auto& slot : slots)
	{
		if (slot.completion)
		{
			valid = slot.completion->Wait();
			if (!valid)
			{
				return valid;
			}
			slot.completion.reset();
		}
		if (slot.command)
		{
			valid = slot.command->Reset();
			if (!valid)
			{
				return valid;
			}
		}
		slot.retained.clear();
		slot.image = {};
		slot.swapchain.reset();
	}
	return {};
}
} // namespace Ursa::Rendering
