#pragma once

#include <Gui/GuiSession.hpp>
#include <Rendering/Gui/ImGuiRenderer.hpp>
#include <Rendering/Resources/RenderFrameContext.hpp>

namespace Ursa::Gui
{
class Canvas final
{
public:
	static Result<std::shared_ptr<Canvas>> Create(Rhi::DevicePtr device, const VirtualFileSystem& files);
	~Canvas();
	Result<void> Build(const Session& session, Point viewport);
	Result<void> Record(Rendering::RenderFrameQueue& frames, const Rhi::CommandListPtr& command,
	                    const Rhi::TexturePtr& target);
	void Discard();
	bool HasDrawData() const;
	Float32 MeasureText(const String& text) const;

private:
	Canvas() = default;
	ImGuiContext* context = nullptr;
	Vector<UInt8> fontBytes;
	std::unique_ptr<Rendering::ImGuiRenderer> renderer;

	struct TextureEntry
	{
		MaterialTexturePtr source;
		ImTextureID id;
	};

	std::unordered_map<const MaterialTexture*, TextureEntry> textures;
	bool ready = false;
};
} // namespace Ursa::Gui
