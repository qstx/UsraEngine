#include <Assets/AssetPreviewTexture.hpp>
#include <Core/StringInterop.hpp>
#include <stdexcept>

namespace Ursa::Editor
{
namespace
{
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

Result<MaterialTexturePtr> CreateTexturePreviewView(const MaterialTexturePtr& source, UInt32 mip,
                                                    TexturePreviewChannel channel)
{
	if (!source || mip >= source->GetMipLevels())
	{
		return Result<MaterialTexturePtr>::Failure("Texture preview mip is unavailable");
	}
	if (channel < TexturePreviewChannel::Rgba || channel > TexturePreviewChannel::Alpha)
	{
		return Result<MaterialTexturePtr>::Failure("Texture preview channel is unavailable");
	}
	const UInt32 width = mip ? source->GetMipmaps()[mip - 1].width : source->GetWidth();
	const UInt32 height = mip ? source->GetMipmaps()[mip - 1].height : source->GetHeight();
	const auto& original = mip ? source->GetMipmaps()[mip - 1].pixels : source->GetPixels();
	Vector<UInt8> pixels = original;
	for (Size offset = 0; offset < pixels.size(); offset += 4)
	{
		if (channel == TexturePreviewChannel::Rgb)
		{
			pixels[offset + 3] = 255;
		}
		else if (channel != TexturePreviewChannel::Rgba)
		{
			const Size component = static_cast<Size>(channel) - static_cast<Size>(TexturePreviewChannel::Red);
			const UInt8 value = pixels[offset + component];
			pixels[offset] = value;
			pixels[offset + 1] = value;
			pixels[offset + 2] = value;
			pixels[offset + 3] = 255;
		}
	}
	const auto space = channel == TexturePreviewChannel::Alpha ? TextureColorSpace::Linear : source->GetColorSpace();
	return MaterialTexture::Create(width, height, space, std::move(pixels), {}, source->DefaultSampler());
}

Result<void> UploadTexturePreview(Rhi::DevicePtr device, ImGuiBridge& gui, const MaterialTexturePtr& texture,
                                  ImTextureID& textureId, Rhi::TexturePtr& gpuTexture, Rhi::SamplerPtr& gpuSampler)
{
	if (!device || !texture)
	{
		return Result<void>::Failure("Texture preview needs a device and decoded texture");
	}
	Ursa::Size uploadBytes = texture->GetPixels().size();
	for (const TextureMip& mipmap : texture->GetMipmaps())
	{
		uploadBytes += mipmap.pixels.size();
	}
	auto upload = device->CreateBuffer({uploadBytes, Rhi::BufferUsage::TransferSource, Rhi::Memory::Upload});
	if (!upload)
	{
		return Result<void>::Failure(upload.error);
	}
	Ursa::Size offset = 0;
	auto written = upload.value->Write(offset, texture->GetPixels().data(), texture->GetPixels().size());
	if (!written)
	{
		return written;
	}
	offset += texture->GetPixels().size();
	for (const TextureMip& mipmap : texture->GetMipmaps())
	{
		written = upload.value->Write(offset, mipmap.pixels.data(), mipmap.pixels.size());
		if (!written)
		{
			return written;
		}
		offset += mipmap.pixels.size();
	}
	const Rhi::Format format =
	    texture->GetColorSpace() == TextureColorSpace::Srgb ? Rhi::Format::RgbA8Srgb : Rhi::Format::RgbA8Unorm;
	auto createdTexture = device->CreateTexture({texture->GetWidth(), texture->GetHeight(), format,
	                                             Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination,
	                                             texture->GetMipLevels()});
	if (!createdTexture)
	{
		return Result<void>::Failure(createdTexture.error);
	}
	try
	{
		auto command = Take(device->CreateCommandList());
		Done(command->Begin());
		Done(command->Barrier(createdTexture.value, Rhi::ResourceState::Undefined,
		                      Rhi::ResourceState::TransferDestination));
		offset = 0;
		for (Ursa::UInt32 mipLevel = 0; mipLevel < texture->GetMipLevels(); ++mipLevel)
		{
			const Ursa::Size mipBytes =
			    mipLevel == 0 ? texture->GetPixels().size() : texture->GetMipmaps()[mipLevel - 1].pixels.size();
			Done(command->CopyBufferToTexture(upload.value, createdTexture.value, mipLevel, offset));
			offset += mipBytes;
		}
		Done(command->Barrier(createdTexture.value, Rhi::ResourceState::TransferDestination,
		                      Rhi::ResourceState::ShaderRead));
		Done(command->End());
		auto fence = Take(device->Submit(command));
		Done(fence->Wait());
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
	const MaterialSampler samplerSettings = texture->DefaultSampler();
	auto createdSampler = device->CreateSampler(
	    {samplerSettings.linear, samplerSettings.repeat, static_cast<Ursa::Float32>(texture->GetMipLevels() - 1)});
	if (!createdSampler)
	{
		return Result<void>::Failure(createdSampler.error);
	}
	auto registered = gui.RegisterTexture(createdTexture.value, createdSampler.value);
	if (!registered)
	{
		return Result<void>::Failure(registered.error);
	}
	textureId = registered.value;
	gpuTexture = std::move(createdTexture.value);
	gpuSampler = std::move(createdSampler.value);
	return {};
}

} // namespace Ursa::Editor
