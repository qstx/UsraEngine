#include <Rendering/Resources/RenderFrameContext.hpp>
#include <iostream>
#include <stdexcept>

using namespace Ursa;
using namespace Ursa::Rendering;

namespace
{
template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		Rhi::DeviceDesc deviceDesc;
#ifndef NDEBUG
		deviceDesc.validation = true;
#endif
		auto device = Take(Rhi::CreateDevice(deviceDesc));
		auto frames = Take(RenderFrameQueue::Create(device, 256, 1024));
		Array<Rhi::BufferPtr, 2> readbacks;
		Array<UploadSlice, 2> slices;
		Array<Rhi::FencePtr, 2> completions;
		auto marker = std::make_shared<int>(7);
		std::weak_ptr<int> retained = marker;
		for (UInt32 index = 0; index < 2; ++index)
		{
			auto* frame = Take(frames->Begin());
			Expect(frame->Slot() == index && frames->Statistics().slotReuses == 0,
			       "Second frame waited for the preceding frame slot");
			const Array<UInt32, 4> values{index + 1, index + 2, index + 3, index + 4};
			slices[index] = Take(frames->Upload(values.data(), sizeof(values), 16));
			Expect(!frames->Upload(values.data(), 4096, 16), "Frame upload budget was ignored");
			readbacks[index] = Take(
			    device->CreateBuffer({sizeof(values), Rhi::BufferUsage::TransferDestination, Rhi::Memory::Readback}));
			Done(frame->Command()->Barrier(slices[index].buffer, Rhi::ResourceState::Undefined,
			                               Rhi::ResourceState::TransferSource));
			Done(frame->Command()->Barrier(readbacks[index], Rhi::ResourceState::Undefined,
			                               Rhi::ResourceState::TransferDestination));
			Done(frame->Command()->CopyBuffer(slices[index].buffer, readbacks[index], sizeof(values),
			                                  slices[index].offset));
			if (index == 0)
			{
				Done(frames->Retain(marker));
				marker.reset();
			}
			completions[index] = Take(frames->Complete());
			Expect(!retained.expired(), "Pending frame resource was released before its slot was reused");
		}
		Expect(slices[0].buffer != slices[1].buffer, "Two in-flight frames share mutable upload storage");
		auto* reused = Take(frames->Begin());
		Expect(reused->Slot() == 0 && frames->Statistics().slotReuses == 1 && retained.expired(),
		       "Only the reused frame slot should reclaim its resources");
		const Array<UInt32, 4> replacement{20, 21, 22, 23};
		auto thirdSlice = Take(frames->Upload(replacement.data(), sizeof(replacement), 16));
		Expect(thirdSlice.buffer == slices[0].buffer && frames->Statistics().uploadBuffersCreated == 2,
		       "Completed upload buffer was not reused");
		Done(frames->Abort());
		for (UInt32 index = 0; index < 2; ++index)
		{
			Done(completions[index]->Wait());
			Array<UInt32, 4> values{};
			Done(readbacks[index]->Read(0, values.data(), sizeof(values)));
			Expect(values[0] == index + 1 && values[3] == index + 4,
			       "Frame upload overwrote data still used by GPU work");
		}
		Done(frames->Drain());
		Done(device->WaitIdle());
		std::cout << "Two-slot render frame and upload reuse tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
