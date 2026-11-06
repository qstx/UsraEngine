#include <Gui/GuiCanvas.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace Ursa::Gui
{
namespace
{
bool ValidFont(const Vector<UInt8>& bytes)
{
	if (bytes.size() < 12)
	{
		return false;
	}
	const auto word = [&bytes](Size i) { return (UInt32(bytes[i]) << 8) | bytes[i + 1]; };
	if (!(bytes[0] == 0 && bytes[1] == 1 && bytes[2] == 0 && bytes[3] == 0) &&
	    std::memcmp(bytes.data(), "OTTO", 4) != 0)
	{
		return false;
	}
	const Size count = word(4);
	if (!count || count > (bytes.size() - 12) / 16)
	{
		return false;
	}
	const auto dword = [&word](Size i) { return (UInt64(word(i)) << 16) | word(i + 2); };
	for (Size i = 0; i < count; ++i)
	{
		const Size table = 12 + 16 * i;
		const UInt64 offset = dword(table + 8), length = dword(table + 12);
		if (offset > bytes.size() || length > bytes.size() - offset)
		{
			return false;
		}
	}
	return true;
}
} // namespace

Result<std::shared_ptr<Canvas>> Canvas::Create(Rhi::DevicePtr device, const VirtualFileSystem& files)
{
	const auto fontPath = VirtualPath::Parse("ursa:Fonts/Runtime/NotoSansSC-VF.ttf");
	auto font = files.Read(fontPath.value);
	if (!font || !ValidFont(font.value) || font.value.size() > static_cast<Size>((std::numeric_limits<int>::max)()))
	{
		return Result<std::shared_ptr<Canvas>>::Failure(
		    "Runtime GUI font ursa:Fonts/Runtime/NotoSansSC-VF.ttf: " +
		    (font ? String("invalid or truncated OpenType font") : font.error));
	}
	auto created = Rendering::ImGuiRenderer::Create(
	    std::move(device), files, VirtualPath::Parse("ursa:Shaders/Runtime/GUI.ursashaderasset").value);
	if (!created)
	{
		return Result<std::shared_ptr<Canvas>>::Failure("Runtime GUI shader: " + created.error);
	}
	auto canvas = std::shared_ptr<Canvas>(new Canvas());
	canvas->renderer = std::move(created.value);
	canvas->fontBytes = std::move(font.value);
	auto* previous = ImGui::GetCurrentContext();
	canvas->context = ImGui::CreateContext();
	ImGui::SetCurrentContext(previous);
	Rendering::ImGuiContextScope scope(canvas->context);
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
	ImFontConfig config{};
	config.FontDataOwnedByAtlas = false;
	io.FontDefault = io.Fonts->AddFontFromMemoryTTF(canvas->fontBytes.data(),
	                                                static_cast<int>(canvas->fontBytes.size()), 18.f, &config);
	if (!io.FontDefault)
	{
		return Result<std::shared_ptr<Canvas>>::Failure("Runtime GUI font could not be initialized");
	}
	// Prepare font metrics independently of user events. Layout/input never
	// start a Dear ImGui frame or dispatch project OnGUI for atlas preparation.
	io.DisplaySize = {1, 1};
	io.DeltaTime = 1.f / 60.f;
	ImGui::NewFrame();
	ImGui::Render();
	return Result<std::shared_ptr<Canvas>>::Success(std::move(canvas));
}

Canvas::~Canvas()
{
	if (context)
	{
		ImGui::DestroyContext(context);
	}
}

Result<void> Canvas::Build(const Session& session, Point viewport)
{
	ready = false;
	if (viewport.x <= 0 || viewport.y <= 0)
	{
		return Result<void>::Failure("GUI Repaint requires a drawable target");
	}
	Rendering::ImGuiContextScope scope(context);
	auto& io = ImGui::GetIO();
	io.DisplaySize = {viewport.x, viewport.y};
	io.DisplayFramebufferScale = {1, 1};
	io.DeltaTime = 1.f / 60.f;
	std::unordered_set<const MaterialTexture*> usedTextures;
	for (const auto& command : session.Commands())
	{
		if (!command.texture)
		{
			continue;
		}
		usedTextures.insert(command.texture.get());
		if (!textures.count(command.texture.get()))
		{
			const auto& source = command.texture;
			auto uploaded = renderer->UploadImage(source->GetPixels().data(), source->GetWidth(), source->GetHeight());
			if (!uploaded)
			{
				return Result<void>::Failure(uploaded.error);
			}
			textures.emplace(source.get(), TextureEntry{source, uploaded.value});
		}
	}
	for (auto it = textures.begin(); it != textures.end();)
	{
		if (!usedTextures.count(it->first))
		{
			renderer->UnregisterTexture(it->second.id);
			it = textures.erase(it);
		}
		else
		{
			++it;
		}
	}
	ImGui::NewFrame();
	auto* draw = ImGui::GetBackgroundDrawList();
	for (const auto& command : session.Commands())
	{
		const auto& r = command.rect;
		const auto& clip = command.clip;
		draw->PushClipRect({clip.x, clip.y}, {clip.x + clip.width, clip.y + clip.height}, true);
		const ImVec2 minimum{r.x, r.y}, maximum{r.x + r.width, r.y + r.height};
		switch (command.type)
		{
			case DrawType::Fill:
				draw->AddRectFilled(minimum, maximum, command.color);
				break;
			case DrawType::Border:
				draw->AddRect(minimum, maximum, command.color);
				break;
			case DrawType::Text:
				draw->PushClipRect(minimum, maximum, true);
				draw->AddText({r.x + 4, r.y + std::max(0.f, (r.height - 18.f) / 2)}, command.color,
				              command.text.c_str());
				draw->PopClipRect();
				break;
			case DrawType::Texture:
				draw->AddImage(textures.at(command.texture.get()).id, minimum, maximum);
				break;
		}
		draw->PopClipRect();
	}
	ImGui::Render();
	auto uploaded = renderer->UpdateAtlas(ImGui::GetDrawData());
	ready = static_cast<bool>(uploaded);
	return uploaded;
}

Result<void> Canvas::Record(Rendering::RenderFrameQueue& frames, const Rhi::CommandListPtr& command,
                            const Rhi::TexturePtr& target)
{
	if (!ready || !target)
	{
		return {};
	}
	Rendering::ImGuiContextScope scope(context);
	const auto* data = ImGui::GetDrawData();
	if (!data || data->TotalVtxCount == 0 || data->TotalIdxCount == 0)
	{
		return {};
	}
	// The executor has already transitioned the final scene color. This pass loads
	// it and blends display-space GUI after all scene postprocessing.
	if (!command)
	{
		return Result<void>::Failure("GUI composite requires an active render submission");
	}
	Rhi::RenderTarget pass;
	pass.colors.push_back({target, Rhi::AttachmentLoadOp::Load});
	auto result = command->BeginRendering(pass);
	if (!result)
	{
		return result;
	}
	if (result)
	{
		result = command->SetViewport(
		    {0, 0, static_cast<Float32>(target->desc.width), static_cast<Float32>(target->desc.height), 0, 1});
	}
	if (result)
	{
		result = renderer->Record(command, target->desc.format, target->desc.width, target->desc.height,
		                          ImGui::GetDrawData());
	}
	if (result)
	{
		result = frames.Retain(renderer->LastRecording());
	}
	auto ended = command->EndRendering();
	return result ? ended : result;
}

void Canvas::Discard()
{
	ready = false;
}

bool Canvas::HasDrawData() const
{
	return ready;
}

Float32 Canvas::MeasureText(const String& text) const
{
	Rendering::ImGuiContextScope scope(context);
	return ImGui::GetIO().FontDefault->CalcTextSizeA(18.f, (std::numeric_limits<float>::max)(), 0, text.c_str()).x;
}
} // namespace Ursa::Gui
