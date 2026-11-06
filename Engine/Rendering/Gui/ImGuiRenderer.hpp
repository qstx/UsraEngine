#pragma once

#include <Core/Containers.hpp>
#include <Rhi/RHI.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <imgui.h>
#include <memory>

namespace Ursa::Rendering
{
// Temporarily selects a context without changing its ownership.
class ImGuiContextScope final
{
public:
	explicit ImGuiContextScope(ImGuiContext* selected) : previous(ImGui::GetCurrentContext())
	{
		ImGui::SetCurrentContext(selected);
	}

	~ImGuiContextScope()
	{
		ImGui::SetCurrentContext(previous);
	}

	ImGuiContextScope(const ImGuiContextScope&) = delete;
	ImGuiContextScope& operator=(const ImGuiContextScope&) = delete;

private:
	ImGuiContext* previous;
};

// Context-independent renderer. The caller owns the context and frame scheduling.
class ImGuiRenderer final
{
public:
	static Result<std::unique_ptr<ImGuiRenderer>> Create(Rhi::DevicePtr selectedDevice, const VirtualFileSystem& files,
	                                                     const VirtualPath& selectedShader);

	Result<void> ReloadShaders(const VirtualFileSystem& files);
	Result<void> UpdateAtlas(const ImDrawData* drawData);
	Result<ImTextureID> UploadImage(const UInt8* pixels, UInt32 width, UInt32 height);
	Result<ImTextureID> RegisterTexture(Rhi::TexturePtr texture, Rhi::SamplerPtr sampler);
	void UnregisterTexture(ImTextureID id);

	Result<void> Record(const Rhi::CommandListPtr& command, Rhi::Format colorFormat, UInt32 framebufferWidth,
	                    UInt32 framebufferHeight, const ImDrawData* drawData);

	Size RegisteredTextureCount() const
	{
		return groups.size();
	}

	// Keep this token with the submission until its fence completes. It owns
	// every buffer, pipeline and texture used by the most recent Record call.
	std::shared_ptr<void> LastRecording() const
	{
		return lastRecording;
	}

private:
	ImGuiRenderer(Rhi::DevicePtr selectedDevice, VirtualPath selectedShader);
	Result<void> LoadShaders(const VirtualFileSystem& files);
	Result<void> UploadAtlas(ImTextureData* data);

	struct Recording;
	std::shared_ptr<Recording> lastRecording;
	Rhi::DevicePtr device;
	VirtualPath shaderPath;
	Rhi::LayoutPtr layout;
	Rhi::ShaderPtr vertexShader;
	Rhi::ShaderPtr fragmentShader;
	Ursa::UInt32 textureBinding = 0;
	Ursa::UInt32 samplerBinding = 0;
	Rhi::BlendState blendState;
	Rhi::CullMode cullMode = Rhi::CullMode::None;
	Rhi::Compare depthCompare = Rhi::Compare::Less;
	bool blendEnabled = false;
	bool depthTestEnabled = false;
	bool depthWriteEnabled = false;
	Rhi::SamplerPtr atlasSampler;
	UnorderedMap<int, Rhi::PipelinePtr> pipelines;
	UnorderedMap<ImTextureID, Rhi::BindGroupPtr> groups;
	UnorderedMap<ImTextureID, Vector<Rhi::BindingResource>> textureResources;
	UnorderedMap<ImTextureData*, ImTextureID> atlasIds;
	ImTextureID nextTextureId = 1;
};
} // namespace Ursa::Rendering
