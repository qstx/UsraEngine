#include <Rendering/Gui/ImGuiRenderer.hpp>
#include <Core/Memory/MemoryContainers.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderReflection.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Ursa::Rendering
{
namespace
{
struct GuiVertex
{
	Ursa::Float32 position[2];
	Ursa::Float32 uv[2];
	Ursa::Float32 color[4];
};

Result<void> ApplyImGuiRenderState(const Shader::RenderStateDeclaration& state, Rhi::BlendState& blendState,
                                   Rhi::CullMode& cullMode, bool& blendEnabled, bool& depthTestEnabled,
                                   bool& depthWriteEnabled)
{
	if (state.name == "Blend")
	{
		if (state.value == "Off")
		{
			blendEnabled = false;
			blendState.enabled = false;
			return {};
		}
		if (state.value != "SrcAlpha OneMinusSrcAlpha")
		{
			return Result<void>::Failure("Unsupported ImGui Shader Blend state: " + state.value);
		}
		blendEnabled = true;
		blendState.enabled = true;
		blendState.sourceColor = Rhi::BlendFactor::SourceAlpha;
		blendState.destinationColor = Rhi::BlendFactor::OneMinusSourceAlpha;
		blendState.sourceAlpha = Rhi::BlendFactor::SourceAlpha;
		blendState.destinationAlpha = Rhi::BlendFactor::OneMinusSourceAlpha;
		return {};
	}
	if (state.name == "Cull")
	{
		if (state.value == "Off")
		{
			cullMode = Rhi::CullMode::None;
			return {};
		}
		return Result<void>::Failure("Unsupported ImGui Shader Cull state: " + state.value);
	}
	if (state.name == "ZTest")
	{
		if (state.value == "Off" || state.value == "Disabled")
		{
			depthTestEnabled = false;
			return {};
		}
		return Result<void>::Failure("Unsupported ImGui Shader ZTest state: " + state.value);
	}
	if (state.name == "ZWrite")
	{
		if (state.value == "Off")
		{
			depthWriteEnabled = false;
			return {};
		}
		return Result<void>::Failure("Unsupported ImGui Shader ZWrite state: " + state.value);
	}
	return Result<void>::Failure("Unsupported ImGui Shader render state: " + state.name);
}
} // namespace

struct ImGuiRenderer::Recording
{
	Rhi::BufferPtr vertexBuffer;
	Rhi::BufferPtr indexBuffer;
	Rhi::PipelinePtr pipeline;
	Vector<Rhi::BindGroupPtr> groups;
	Vector<Rhi::BindingResource> textures;
};

ImGuiRenderer::ImGuiRenderer(Rhi::DevicePtr selectedDevice, VirtualPath selectedShader)
    : device(std::move(selectedDevice)), shaderPath(std::move(selectedShader))
{
}

Result<std::unique_ptr<ImGuiRenderer>>
ImGuiRenderer::Create(Rhi::DevicePtr selectedDevice, const VirtualFileSystem& files, const VirtualPath& selectedShader)
{
	if (!selectedDevice)
	{
		return Result<std::unique_ptr<ImGuiRenderer>>::Failure("ImGui renderer requires a device");
	}
	auto renderer = std::unique_ptr<ImGuiRenderer>(new ImGuiRenderer(std::move(selectedDevice), selectedShader));
	auto loaded = renderer->LoadShaders(files);
	if (!loaded)
	{
		return Result<std::unique_ptr<ImGuiRenderer>>::Failure(loaded.error);
	}
	auto sampler = renderer->device->CreateSampler({true, false});
	if (!sampler)
	{
		return Result<std::unique_ptr<ImGuiRenderer>>::Failure(sampler.error);
	}
	renderer->atlasSampler = std::move(sampler.value);
	return Result<std::unique_ptr<ImGuiRenderer>>::Success(std::move(renderer));
}

Result<void> ImGuiRenderer::LoadShaders(const VirtualFileSystem& files)
{
	auto shader = Shader::LoadShaderAssetResource(files, shaderPath);
	if (!shader)
	{
		return Result<void>::Failure(shader.error);
	}
	if (shader.value.subShaders.empty() || shader.value.subShaders.front().passes.empty() ||
	    shader.value.subShaders.front().passes.front().variants.empty())
	{
		return Result<void>::Failure("Imported ImGui Shader has no compiled graphics Pass");
	}
	const auto& subShader = shader.value.subShaders.front();
	const auto& pass = subShader.passes.front();
	const auto& variant = pass.variants.front();
	for (const Shader::RenderStateDeclaration& state : subShader.renderStates)
	{
		auto applied =
		    ApplyImGuiRenderState(state, blendState, cullMode, blendEnabled, depthTestEnabled, depthWriteEnabled);
		if (!applied)
		{
			return applied;
		}
	}
	for (const Shader::RenderStateDeclaration& state : pass.renderStates)
	{
		auto applied =
		    ApplyImGuiRenderState(state, blendState, cullMode, blendEnabled, depthTestEnabled, depthWriteEnabled);
		if (!applied)
		{
			return applied;
		}
	}
	auto reflection = Shader::BuildShaderBindingLayout(shader.value, variant);
	if (!reflection)
	{
		return Result<void>::Failure(reflection.error);
	}
	for (const Rhi::LayoutBinding& binding : reflection.value.descriptors)
	{
		if (binding.type == Rhi::BindingType::SampledTexture)
		{
			textureBinding = binding.binding;
		}
		else if (binding.type == Rhi::BindingType::Sampler)
		{
			samplerBinding = binding.binding;
		}
		else
		{
			return Result<void>::Failure("ImGui Shader declares an unsupported descriptor type");
		}
	}
	if (reflection.value.descriptors.size() != 2)
	{
		return Result<void>::Failure("ImGui Shader must declare one texture and one sampler");
	}
	auto layoutResult = Shader::CreateShaderBindGroupLayout(reflection.value, *device);
	if (!layoutResult)
	{
		return Result<void>::Failure(layoutResult.error);
	}
	layout = std::move(layoutResult.value);
	for (const Shader::ShaderStageAsset& stage : variant.stages)
	{
		if (stage.target != Shader::ShaderTarget::VulkanSpirv)
		{
			return Result<void>::Failure("ImGui Shader has no Vulkan SPIR-V stage");
		}
		Rhi::ShaderStage stageType;
		if (stage.stage == Shader::ProgramStage::Vertex)
		{
			stageType = Rhi::ShaderStage::Vertex;
		}
		else if (stage.stage == Shader::ProgramStage::Fragment)
		{
			stageType = Rhi::ShaderStage::Fragment;
		}
		else
		{
			return Result<void>::Failure("ImGui Shader cannot contain a compute stage");
		}
		auto created = device->CreateShader({stageType, Rhi::ShaderFormat::Spirv, stage.spirv, stage.entryPoint});
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		if (stage.stage == Shader::ProgramStage::Vertex)
		{
			vertexShader = std::move(created.value);
		}
		else
		{
			fragmentShader = std::move(created.value);
		}
	}
	if (!vertexShader || !fragmentShader)
	{
		return Result<void>::Failure("ImGui Shader requires vertex and fragment stages");
	}
	return {};
}

Result<ImTextureID> ImGuiRenderer::RegisterTexture(Rhi::TexturePtr texture, Rhi::SamplerPtr sampler)
{
	if (!texture || !sampler)
	{
		return Result<ImTextureID>::Failure("ImGui image requires a texture and sampler");
	}
	if (!Rhi::Has(texture->desc.usage, Rhi::TextureUsage::Sampled))
	{
		return Result<ImTextureID>::Failure("ImGui image texture must have Sampled usage");
	}
	Vector<Rhi::BindingResource> resources;
	resources.push_back({textureBinding, {}, std::move(texture), {}});
	resources.push_back({samplerBinding, {}, {}, std::move(sampler)});
	auto group = device->CreateBindGroup({layout, resources});
	if (!group)
	{
		return Result<ImTextureID>::Failure(group.error);
	}
	const ImTextureID id = nextTextureId++;
	groups.emplace(id, std::move(group.value));
	textureResources.emplace(id, std::move(resources));
	return Result<ImTextureID>::Success(id);
}

void ImGuiRenderer::UnregisterTexture(ImTextureID id)
{
	groups.erase(id);
	textureResources.erase(id);
}

Result<ImTextureID> ImGuiRenderer::UploadImage(const UInt8* pixels, UInt32 width, UInt32 height)
{
	if (!pixels || !width || !height || width > std::numeric_limits<Size>::max() / 4 / height)
	{
		return Result<ImTextureID>::Failure("Invalid ImGui image dimensions or pixels");
	}
	const Size byteCount = static_cast<Size>(width) * height * 4;
	auto upload = device->CreateBuffer({byteCount, Rhi::BufferUsage::TransferSource, Rhi::Memory::Upload});
	if (!upload)
	{
		return Result<ImTextureID>::Failure(upload.error);
	}
	auto written = upload.value->Write(0, pixels, byteCount);
	if (!written)
	{
		return Result<ImTextureID>::Failure(written.error);
	}
	auto texture = device->CreateTexture(
	    {width, height, Rhi::Format::RgbA8Unorm, Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination});
	if (!texture)
	{
		return Result<ImTextureID>::Failure(texture.error);
	}
	auto command = device->CreateCommandList();
	if (!command)
	{
		return Result<ImTextureID>::Failure(command.error);
	}
	auto result = command.value->Begin();
	if (result)
	{
		result = command.value->Barrier(texture.value, Rhi::ResourceState::Undefined,
		                                Rhi::ResourceState::TransferDestination);
	}
	if (result)
	{
		result = command.value->CopyBufferToTexture(upload.value, texture.value);
	}
	if (result)
	{
		result = command.value->Barrier(texture.value, Rhi::ResourceState::TransferDestination,
		                                Rhi::ResourceState::ShaderRead);
	}
	if (result)
	{
		result = command.value->End();
	}
	if (!result)
	{
		return Result<ImTextureID>::Failure(result.error);
	}
	auto submitted = device->Submit(command.value);
	if (!submitted)
	{
		return Result<ImTextureID>::Failure(submitted.error);
	}
	result = submitted.value->Wait();
	if (!result)
	{
		return Result<ImTextureID>::Failure(result.error);
	}
	auto registered = RegisterTexture(std::move(texture.value), atlasSampler);
	if (!registered)
	{
		return Result<ImTextureID>::Failure(registered.error);
	}
	return registered;
}

Result<void> ImGuiRenderer::UploadAtlas(ImTextureData* data)
{
	if (!data || data->Width <= 0 || data->Height <= 0 || !data->Pixels)
	{
		return Result<void>::Failure("ImGui requested an invalid font atlas");
	}
	const auto width = static_cast<Ursa::UInt32>(data->Width);
	const auto height = static_cast<Ursa::UInt32>(data->Height);
	if (width > std::numeric_limits<Ursa::Size>::max() / 4 / height)
	{
		return Result<void>::Failure("ImGui font atlas size overflow");
	}
	const Ursa::Size byteCount = static_cast<Ursa::Size>(width) * height * 4;
	Ursa::Vector<Ursa::UInt8> rgba(byteCount);
	if (data->Format == ImTextureFormat_RGBA32)
	{
		std::memcpy(rgba.data(), data->GetPixels(), byteCount);
	}
	else if (data->Format == ImTextureFormat_Alpha8)
	{
		for (Ursa::Size i = 0; i < static_cast<Ursa::Size>(width) * height; ++i)
		{
			rgba[i * 4 + 0] = 255;
			rgba[i * 4 + 1] = 255;
			rgba[i * 4 + 2] = 255;
			rgba[i * 4 + 3] = data->Pixels[i];
		}
	}
	else
	{
		return Result<void>::Failure("Unsupported ImGui font atlas format");
	}
	auto registered = UploadImage(rgba.data(), width, height);
	if (!registered)
	{
		return Result<void>::Failure(registered.error);
	}
	if (auto found = atlasIds.find(data); found != atlasIds.end())
	{
		UnregisterTexture(found->second);
	}
	atlasIds[data] = registered.value;
	data->SetTexID(registered.value);
	data->SetStatus(ImTextureStatus_OK);
	return {};
}

Result<void> ImGuiRenderer::UpdateAtlas(const ImDrawData* drawData)
{
	if (!drawData || !drawData->Textures)
	{
		return {};
	}
	for (ImTextureData* data : *drawData->Textures)
	{
		if (data->Status == ImTextureStatus_WantCreate || data->Status == ImTextureStatus_WantUpdates)
		{
			auto uploaded = UploadAtlas(data);
			if (!uploaded)
			{
				return uploaded;
			}
		}
		else if (data->Status == ImTextureStatus_WantDestroy)
		{
			if (auto found = atlasIds.find(data); found != atlasIds.end())
			{
				UnregisterTexture(found->second);
				atlasIds.erase(found);
			}
			data->SetTexID(ImTextureID_Invalid);
			data->SetStatus(ImTextureStatus_Destroyed);
		}
	}
	return {};
}

Result<void> ImGuiRenderer::ReloadShaders(const VirtualFileSystem& files)
{
	ImGuiRenderer replacement(device, shaderPath);
	auto loaded = replacement.LoadShaders(files);
	if (!loaded)
	{
		return loaded;
	}
	UnorderedMap<ImTextureID, Rhi::BindGroupPtr> replacementGroups;
	for (const auto& entry : textureResources)
	{
		auto resources = entry.second;
		for (auto& resource : resources)
		{
			resource.binding = resource.texture ? replacement.textureBinding : replacement.samplerBinding;
		}
		auto group = device->CreateBindGroup({replacement.layout, std::move(resources)});
		if (!group)
		{
			return Result<void>::Failure(group.error);
		}
		replacementGroups.emplace(entry.first, std::move(group.value));
	}
	layout = std::move(replacement.layout);
	vertexShader = std::move(replacement.vertexShader);
	fragmentShader = std::move(replacement.fragmentShader);
	blendState = replacement.blendState;
	cullMode = replacement.cullMode;
	blendEnabled = replacement.blendEnabled;
	depthTestEnabled = replacement.depthTestEnabled;
	depthWriteEnabled = replacement.depthWriteEnabled;
	textureBinding = replacement.textureBinding;
	samplerBinding = replacement.samplerBinding;
	groups = std::move(replacementGroups);
	pipelines.clear();
	return {};
}

Result<void> ImGuiRenderer::Record(const Rhi::CommandListPtr& command, Rhi::Format colorFormat,
                                   Ursa::UInt32 framebufferWidth, Ursa::UInt32 framebufferHeight,
                                   const ImDrawData* data)
{
	Memory::TemporaryScope recordingMemory;
	lastRecording.reset();
	if (!command || !framebufferWidth || !framebufferHeight)
	{
		return Result<void>::Failure("ImGui record requires a command list and nonzero framebuffer");
	}
	if (!data || data->TotalVtxCount == 0 || data->TotalIdxCount == 0)
	{
		return {};
	}
	const int formatKey = static_cast<int>(colorFormat);
	auto pipeline = pipelines.find(formatKey);
	if (pipeline == pipelines.end())
	{
		Rhi::GraphicsPipelineDesc desc;
		desc.vertex = vertexShader;
		desc.fragment = fragmentShader;
		desc.layout = layout;
		desc.colorFormat = colorFormat;
		desc.blend = blendEnabled;
		desc.blendState = blendState;
		desc.cullMode = cullMode;
		desc.depthTest = depthTestEnabled;
		desc.depthWrite = depthWriteEnabled;
		desc.depthCompare = depthCompare;
		desc.vertexStride = sizeof(GuiVertex);
		desc.attributes = {
		    {0, 0, Rhi::VertexFormat::Float2}, {1, 8, Rhi::VertexFormat::Float2}, {2, 16, Rhi::VertexFormat::Float4}};
		auto created = device->CreateGraphicsPipeline(desc);
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		pipeline = pipelines.emplace(formatKey, std::move(created.value)).first;
	}
	Ursa::TemporaryVector<GuiVertex> vertices{Memory::TemporaryContainerAllocator(Memory::MemoryTag::Renderer)};
	Ursa::TemporaryVector<Ursa::UInt32> indices{Memory::TemporaryContainerAllocator(Memory::MemoryTag::Renderer)};
	vertices.reserve(static_cast<Ursa::Size>(data->TotalVtxCount));
	indices.reserve(static_cast<Ursa::Size>(data->TotalIdxCount));
	for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex)
	{
		const ImDrawList* list = data->CmdLists[listIndex];
		for (const ImDrawVert& source : list->VtxBuffer)
		{
			GuiVertex vertex{};
			vertex.position[0] = (source.pos.x - data->DisplayPos.x) / data->DisplaySize.x * 2.f - 1.f;
			vertex.position[1] = 1.f - (source.pos.y - data->DisplayPos.y) / data->DisplaySize.y * 2.f;
			vertex.uv[0] = source.uv.x;
			vertex.uv[1] = source.uv.y;
			for (int channel = 0; channel < 4; ++channel)
			{
				vertex.color[channel] = static_cast<Ursa::Float32>((source.col >> (channel * 8)) & 255u) / 255.f;
			}
			vertices.push_back(vertex);
		}
		for (ImDrawIdx index : list->IdxBuffer)
		{
			indices.push_back(static_cast<Ursa::UInt32>(index));
		}
	}
	auto vertexBufferResult =
	    device->CreateBuffer({vertices.size() * sizeof(GuiVertex), Rhi::BufferUsage::Vertex, Rhi::Memory::Upload});
	if (!vertexBufferResult)
	{
		return Result<void>::Failure(vertexBufferResult.error);
	}
	auto indexBufferResult =
	    device->CreateBuffer({indices.size() * sizeof(Ursa::UInt32), Rhi::BufferUsage::Index, Rhi::Memory::Upload});
	if (!indexBufferResult)
	{
		return Result<void>::Failure(indexBufferResult.error);
	}
	auto result = vertexBufferResult.value->Write(0, vertices.data(), vertices.size() * sizeof(GuiVertex));
	if (result)
	{
		result = indexBufferResult.value->Write(0, indices.data(), indices.size() * sizeof(Ursa::UInt32));
	}
	if (!result)
	{
		return result;
	}
	auto recording = std::make_shared<Recording>();
	recording->vertexBuffer = std::move(vertexBufferResult.value);
	recording->indexBuffer = std::move(indexBufferResult.value);
	recording->pipeline = pipeline->second;
	lastRecording = recording;
	const auto& vertexBuffer = recording->vertexBuffer;
	const auto& indexBuffer = recording->indexBuffer;
	result = command->BindPipeline(pipeline->second);
	if (result)
	{
		result = command->BindVertexBuffer(vertexBuffer);
	}
	if (result)
	{
		result = command->BindIndexBuffer(indexBuffer);
	}
	if (!result)
	{
		return result;
	}
	Ursa::UInt32 globalVertex = 0;
	Ursa::UInt32 globalIndex = 0;
	for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex)
	{
		const ImDrawList* list = data->CmdLists[listIndex];
		for (const ImDrawCmd& draw : list->CmdBuffer)
		{
			if (draw.UserCallback)
			{
				if (draw.UserCallback == ImDrawCallback_ResetRenderState)
				{
					result = command->BindPipeline(pipeline->second);
					if (result)
					{
						result = command->BindVertexBuffer(vertexBuffer);
					}
					if (result)
					{
						result = command->BindIndexBuffer(indexBuffer);
					}
				}
				else
				{
					draw.UserCallback(list, &draw);
				}
				if (!result)
				{
					return result;
				}
				continue;
			}
			const Ursa::Float32 minX = (draw.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x;
			const Ursa::Float32 minY = (draw.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y;
			const Ursa::Float32 maxX = (draw.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x;
			const Ursa::Float32 maxY = (draw.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y;
			const Ursa::Int32 left = std::max<Ursa::Int32>(0, static_cast<Ursa::Int32>(std::floor(minX)));
			const Ursa::Int32 top = std::max<Ursa::Int32>(0, static_cast<Ursa::Int32>(std::floor(minY)));
			const Ursa::Int32 right = std::min<Ursa::Int32>(static_cast<Ursa::Int32>(framebufferWidth),
			                                                static_cast<Ursa::Int32>(std::ceil(maxX)));
			const Ursa::Int32 bottom = std::min<Ursa::Int32>(static_cast<Ursa::Int32>(framebufferHeight),
			                                                 static_cast<Ursa::Int32>(std::ceil(maxY)));
			if (right <= left || bottom <= top || draw.ElemCount == 0)
			{
				continue;
			}
			const auto group = groups.find(draw.GetTexID());
			if (group == groups.end())
			{
				return Result<void>::Failure("ImGui draw references an unregistered texture");
			}
			recording->groups.push_back(group->second);
			const auto& resources = textureResources.at(draw.GetTexID());
			recording->textures.insert(recording->textures.end(), resources.begin(), resources.end());
			result = command->BindGroup(group->second);
			if (result)
			{
				result = command->SetScissor(
				    {left, top, static_cast<Ursa::UInt32>(right - left), static_cast<Ursa::UInt32>(bottom - top)});
			}
			if (result)
			{
				result = command->DrawIndexed(draw.ElemCount, 1, 0, globalIndex + draw.IdxOffset,
				                              static_cast<Ursa::Int32>(globalVertex + draw.VtxOffset));
			}
			if (!result)
			{
				return result;
			}
		}
		globalVertex += static_cast<Ursa::UInt32>(list->VtxBuffer.Size);
		globalIndex += static_cast<Ursa::UInt32>(list->IdxBuffer.Size);
	}
	return {};
}
} // namespace Ursa::Rendering
