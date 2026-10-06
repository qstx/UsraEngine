#pragma once

#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Rhi/RHI.hpp>
#include <imgui.h>
#include <memory>

namespace Ursa::Editor
{
// Owns an ImGui context. The caller must finish the GPU submission before starting another frame.
class ImGuiBridge final
{
public:
	static Result<std::unique_ptr<ImGuiBridge>> Create(Rhi::DevicePtr selectedDevice,
	                                                   std::shared_ptr<Window> selectedWindow = {});

	~ImGuiBridge();

	ImGuiBridge(const ImGuiBridge&) = delete;

	ImGuiBridge& operator=(const ImGuiBridge&) = delete;

	Result<void> BeginFrame(Ursa::Float32 deltaSeconds, Ursa::UInt32 width, Ursa::UInt32 height);

	Result<void> EndFrame();

	// Called within an existing color rendering pass. The pass target must match colorFormat.
	Result<void> Record(const Rhi::CommandListPtr& command, Rhi::Format colorFormat, Ursa::UInt32 framebufferWidth,
	                    Ursa::UInt32 framebufferHeight);

	Result<ImTextureID> RegisterTexture(Rhi::TexturePtr texture, Rhi::SamplerPtr sampler);

	void UnregisterTexture(ImTextureID id);

	ImGuiContext* Context() const
	{
		return context;
	}

private:
	explicit ImGuiBridge(Rhi::DevicePtr selectedDevice, std::shared_ptr<Window> selectedWindow);

	Result<void> Initialize();

	Result<void> UpdateAtlas();

	Result<void> UploadAtlas(ImTextureData* data);

	static const char* GetClipboardText(ImGuiContext* guiContext);

	static void SetClipboardText(ImGuiContext* guiContext, const char* text);

	Rhi::DevicePtr device;
	std::shared_ptr<Window> window;
	Ursa::UInt64 subscription = 0;
	ImGuiContext* context = nullptr;
	String clipboardText;
	bool leftControl = false;
	bool rightControl = false;
	bool leftShift = false;
	bool rightShift = false;
	bool leftAlt = false;
	bool rightAlt = false;
	bool leftSuper = false;
	bool rightSuper = false;
	Rhi::LayoutPtr layout;
	Rhi::ShaderPtr vertexShader;
	Rhi::ShaderPtr fragmentShader;
	Rhi::SamplerPtr atlasSampler;
	Rhi::BufferPtr vertexBuffer;
	Rhi::BufferPtr indexBuffer;
	UnorderedMap<int, Rhi::PipelinePtr> pipelines;
	UnorderedMap<ImTextureID, Rhi::BindGroupPtr> groups;
	UnorderedMap<ImTextureData*, ImTextureID> atlasIds;
	ImTextureID nextTextureId = 1;
};
} // namespace Ursa::Editor
