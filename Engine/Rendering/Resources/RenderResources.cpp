#include <Rendering/Resources/RenderResources.hpp>
#include <limits>
#include <Core/StringInterop.hpp>
#include <stdexcept>
#include <atomic>

namespace Ursa::Rendering
{
namespace
{
std::atomic<UInt64> nextRenderTextureIdentity{1};

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}
} // namespace

RenderTextureResource::RenderTextureResource() : identity(nextRenderTextureIdentity.fetch_add(1))
{
	if (!identity)
	{
		throw std::overflow_error("RenderTexture resource identity space is exhausted");
	}
}

bool RenderTextureDescription::operator==(const RenderTextureDescription& other) const
{
	return width == other.width && height == other.height && format == other.format && depthBits == other.depthBits &&
	       randomWrite == other.randomWrite && samples == other.samples;
}

UInt64 RenderTextureDescription::ByteSize() const
{
	const UInt64 pixelBytes = (format == 2 ? 0u : format == 1 ? 8u : 4u) + (depthBits || format == 2 ? 4u : 0u);
	return UInt64(width) * height * pixelBytes;
}

Result<std::shared_ptr<RenderTextureResource>> CreateRenderTexture(Rhi::Device& device,
                                                                   const RenderTextureDescription& description)
{
	const auto capabilities = device.Capabilities();
	if (!description.width || !description.height || description.width > capabilities.maxTextureSize ||
	    description.height > capabilities.maxTextureSize || description.format > 2 || description.samples != 1 ||
	    (description.depthBits != 0 && description.depthBits != 32) || description.ByteSize() > 256u * 1024u * 1024u)
	{
		return Result<std::shared_ptr<RenderTextureResource>>::Failure(
		    "RenderTexture dimensions, format, depth, sample count or 256 MiB budget exceed capabilities");
	}
	auto resource = std::make_shared<RenderTextureResource>();
	resource->description = description;
	if (description.format != 2)
	{
		const auto format = description.format == 1 ? Rhi::Format::RgbA16Float : Rhi::Format::RgbA8Unorm;
		auto supported = device.QueryFormat(format);
		if (!supported || !supported.value.colorAttachment || !supported.value.sampled ||
		    (description.randomWrite && !supported.value.storage))
		{
			return Result<std::shared_ptr<RenderTextureResource>>::Failure(
			    "RenderTexture format does not support requested attachment, sampling or storage usages");
		}
		auto usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled |
		             Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination;
		if (description.randomWrite)
		{
			usage = usage | Rhi::TextureUsage::Storage;
		}
		auto texture = device.CreateTexture({description.width, description.height, format, usage});
		if (!texture)
		{
			return Result<std::shared_ptr<RenderTextureResource>>::Failure(texture.error);
		}
		resource->color = std::move(texture.value);
	}
	if (description.depthBits || description.format == 2)
	{
		auto supported = device.QueryFormat(Rhi::Format::D32Float);
		if (!supported || !supported.value.depthAttachment || !supported.value.sampled ||
		    (description.randomWrite && description.format == 2 && !supported.value.storage))
		{
			return Result<std::shared_ptr<RenderTextureResource>>::Failure(
			    "RenderTexture depth format does not support requested attachment, sampling or storage usages");
		}
		auto usage = Rhi::TextureUsage::DepthAttachment | Rhi::TextureUsage::Sampled |
		             Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination;
		if (description.randomWrite && description.format == 2)
		{
			usage = usage | Rhi::TextureUsage::Storage;
		}
		auto texture = device.CreateTexture({description.width, description.height, Rhi::Format::D32Float, usage});
		if (!texture)
		{
			return Result<std::shared_ptr<RenderTextureResource>>::Failure(texture.error);
		}
		resource->depth = std::move(texture.value);
	}
	return Result<std::shared_ptr<RenderTextureResource>>::Success(std::move(resource));
}

Result<std::shared_ptr<GraphicsBufferResource>> CreateGraphicsBuffer(Rhi::Device& device, UInt32 target, UInt32 count,
                                                                     UInt32 stride)
{
	if (!target || (target & ~31u) || !count || !stride || Size(count) > (std::numeric_limits<Size>::max)() / stride)
	{
		return Result<std::shared_ptr<GraphicsBufferResource>>::Failure(
		    "GraphicsBuffer target, count or stride is invalid");
	}
	const Size byteSize = Size(count) * stride;
	if (byteSize > 256u * 1024u * 1024u ||
	    ((target & (4u | 8u | 16u)) && byteSize > device.Capabilities().maxStorageBufferRange))
	{
		return Result<std::shared_ptr<GraphicsBufferResource>>::Failure(
		    "GraphicsBuffer exceeds the storage range or 256 MiB budget");
	}
	auto usage = Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination;
	if (target & 1u)
	{
		usage = usage | Rhi::BufferUsage::Vertex;
	}
	if (target & 2u)
	{
		usage = usage | Rhi::BufferUsage::Index;
	}
	if (target & (4u | 8u | 16u))
	{
		usage = usage | Rhi::BufferUsage::Storage;
	}
	auto buffer = device.CreateBuffer({byteSize, usage, Rhi::Memory::Gpu});
	if (!buffer)
	{
		return Result<std::shared_ptr<GraphicsBufferResource>>::Failure(buffer.error);
	}
	auto resource = std::make_shared<GraphicsBufferResource>();
	resource->buffer = std::move(buffer.value);
	return Result<std::shared_ptr<GraphicsBufferResource>>::Success(std::move(resource));
}

Result<Vector<UInt8>> ReadRenderTexture(Rhi::Device& device, RenderTextureResource& resource)
{
	try
	{
		const auto texture = resource.color ? resource.color : resource.depth;
		if (!texture)
		{
			throw std::runtime_error("RenderTexture readback requires an attachment");
		}
		auto& state = resource.color ? resource.colorState : resource.DepthState();
		const Size size = Size(texture->desc.width) * texture->desc.height *
		                  (texture->desc.format == Rhi::Format::RgbA16Float ? 8u : 4u);
		auto readback = Take(device.CreateBuffer({size, Rhi::BufferUsage::TransferDestination, Rhi::Memory::Readback}));
		auto command = Take(device.CreateCommandList());
		Done(command->Begin());
		Done(command->Barrier(texture, state, Rhi::ResourceState::TransferSource));
		Done(command->Barrier(readback, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
		Done(command->CopyTextureToBuffer(texture, readback));
		Done(command->End());
		auto completion = Take(device.Submit(command));
		state = Rhi::ResourceState::TransferSource;
		Done(completion->Wait());
		Vector<UInt8> bytes(size);
		Done(readback->Read(0, bytes.data(), size));
		return Result<Vector<UInt8>>::Success(std::move(bytes));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<UInt8>>::Failure(error.what());
	}
}

Result<Vector<UInt8>> ReadGraphicsBuffer(Rhi::Device& device, GraphicsBufferResource& resource, Size offset, Size size)
{
	try
	{
		if (!resource.buffer || !size || offset > resource.buffer->desc.size ||
		    size > resource.buffer->desc.size - offset)
		{
			throw std::runtime_error("GraphicsBuffer readback range is invalid");
		}
		auto readback = Take(device.CreateBuffer({size, Rhi::BufferUsage::TransferDestination, Rhi::Memory::Readback}));
		auto command = Take(device.CreateCommandList());
		Done(command->Begin());
		Done(command->Barrier(resource.buffer, resource.state, Rhi::ResourceState::TransferSource));
		Done(command->Barrier(readback, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
		Done(command->CopyBuffer(resource.buffer, readback, size, offset));
		Done(command->End());
		auto completion = Take(device.Submit(command));
		resource.state = Rhi::ResourceState::TransferSource;
		Done(completion->Wait());
		Vector<UInt8> bytes(size);
		Done(readback->Read(0, bytes.data(), size));
		return Result<Vector<UInt8>>::Success(std::move(bytes));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<UInt8>>::Failure(error.what());
	}
}
} // namespace Ursa::Rendering
