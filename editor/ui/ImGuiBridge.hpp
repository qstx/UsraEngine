#pragma once

#include <rhi/RHI.hpp>
#include <imgui.h>
#include <memory>
#include <unordered_map>

namespace Ursa::Editor
{
// Owns an ImGui context. The caller must finish the GPU submission before starting another frame.
class ImGuiBridge final
{
public:
	static Result<std::unique_ptr<ImGuiBridge>> Create(Rhi::DevicePtr device, std::shared_ptr<Window> window = {});

	~ImGuiBridge();

	ImGuiBridge(const ImGuiBridge&) = delete;

	ImGuiBridge& operator=(const ImGuiBridge&) = delete;

	Result<void> BeginFrame(float deltaSeconds, uint32_t width, uint32_t height);

	Result<void> EndFrame();

	// Called within an existing color rendering pass. The pass target must match colorFormat.
	Result<void> Record(const Rhi::CommandListPtr& command, Rhi::Format colorFormat, uint32_t framebufferWidth,
	                    uint32_t framebufferHeight);

	Result<ImTextureID> RegisterTexture(Rhi::TexturePtr texture, Rhi::SamplerPtr sampler);

	void UnregisterTexture(ImTextureID id);

	ImGuiContext* Context() const
	{
		return context;
	}

private:
	explicit ImGuiBridge(Rhi::DevicePtr device, std::shared_ptr<Window> window);

	Result<void> Initialize();

	Result<void> UpdateAtlas();

	Result<void> UploadAtlas(ImTextureData* data);

	static const char* GetClipboardText(ImGuiContext* context);

	static void SetClipboardText(ImGuiContext* context, const char* text);

	Rhi::DevicePtr device;
	std::shared_ptr<Window> window;
	uint64_t subscription = 0;
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
	std::unordered_map<int, Rhi::PipelinePtr> pipelines;
	std::unordered_map<ImTextureID, Rhi::BindGroupPtr> groups;
	std::unordered_map<ImTextureData*, ImTextureID> atlasIds;
	ImTextureID nextTextureId = 1;
};
} // namespace Ursa::Editor
