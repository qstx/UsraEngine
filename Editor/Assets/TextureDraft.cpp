#include <Ui/EditorIcons.hpp>
#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Assets/TextureDraft.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Ursa::Editor
{
namespace
{
constexpr Ursa::UInt64 maxEditorPixels = 2048ull * 2048ull;

Ursa::UInt8 ToByte(Ursa::Float32 value)
{
	return static_cast<Ursa::UInt8>(std::round(std::clamp(value, 0.f, 1.f) * 255.f));
}
} // namespace

Result<std::shared_ptr<TextureDraft>> TextureDraft::Create(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight,
                                                           TextureColorSpace textureColorSpace,
                                                           Vector<Ursa::UInt8> texturePixels)
{
	if (!textureWidth || !textureHeight || static_cast<Ursa::UInt64>(textureWidth) * textureHeight > maxEditorPixels)
	{
		return Result<std::shared_ptr<TextureDraft>>::Failure("Editor texture dimensions exceed 2048 x 2048");
	}
	auto validated = MaterialTexture::Create(textureWidth, textureHeight, textureColorSpace, texturePixels);
	if (!validated)
	{
		return Result<std::shared_ptr<TextureDraft>>::Failure(validated.error);
	}
	return Result<std::shared_ptr<TextureDraft>>::Success(std::shared_ptr<TextureDraft>(
	    new TextureDraft(textureWidth, textureHeight, textureColorSpace, std::move(texturePixels))));
}

Result<std::shared_ptr<TextureDraft>> TextureDraft::Load(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	auto loaded = LoadTextureResource(filesystem, path);
	if (!loaded)
	{
		return Result<std::shared_ptr<TextureDraft>>::Failure(loaded.error);
	}
	return Create(loaded.value.texture->GetWidth(), loaded.value.texture->GetHeight(),
	              loaded.value.texture->GetColorSpace(), loaded.value.texture->GetPixels());
}

Result<void> TextureDraft::ReplacePixels(Ursa::UInt32 newWidth, Ursa::UInt32 newHeight, TextureColorSpace newColorSpace,
                                         Vector<Ursa::UInt8> newPixels)
{
	auto replacement = Create(newWidth, newHeight, newColorSpace, std::move(newPixels));
	if (!replacement)
	{
		return Result<void>::Failure(replacement.error);
	}
	width = replacement.value->width;
	height = replacement.value->height;
	colorSpace = replacement.value->colorSpace;
	pixels = std::move(replacement.value->pixels);
	return {};
}

Result<void> TextureDraft::Resize(Ursa::UInt32 newWidth, Ursa::UInt32 newHeight)
{
	if (!newWidth || !newHeight || static_cast<Ursa::UInt64>(newWidth) * newHeight > maxEditorPixels)
	{
		return Result<void>::Failure("Editor texture dimensions exceed 2048 x 2048");
	}
	Vector<Ursa::UInt8> resized(static_cast<Ursa::Size>(newWidth) * newHeight * 4, 255);
	const Ursa::UInt32 copyWidth = std::min(width, newWidth);
	const Ursa::UInt32 copyHeight = std::min(height, newHeight);
	for (Ursa::UInt32 y = 0; y < copyHeight; ++y)
	{
		for (Ursa::UInt32 x = 0; x < copyWidth; ++x)
		{
			for (Ursa::UInt32 channel = 0; channel < 4; ++channel)
			{
				resized[(static_cast<Ursa::Size>(y) * newWidth + x) * 4 + channel] =
				    pixels[(static_cast<Ursa::Size>(y) * width + x) * 4 + channel];
			}
		}
	}
	return ReplacePixels(newWidth, newHeight, colorSpace, std::move(resized));
}

Result<void> TextureDraft::SetColorSpace(TextureColorSpace newColorSpace)
{
	if (newColorSpace != TextureColorSpace::Linear && newColorSpace != TextureColorSpace::Srgb)
	{
		return Result<void>::Failure("Invalid Editor texture color space");
	}
	colorSpace = newColorSpace;
	return {};
}

Result<void> TextureDraft::SetPixel(Ursa::UInt32 x, Ursa::UInt32 y, Ursa::Array<Ursa::UInt8, 4> rgba)
{
	if (x >= width || y >= height)
	{
		return Result<void>::Failure("Texture pixel coordinate is outside its dimensions");
	}
	const Ursa::Size offset = (static_cast<Ursa::Size>(y) * width + x) * 4;
	for (Ursa::Size channel = 0; channel < 4; ++channel)
	{
		pixels[offset + channel] = rgba[channel];
	}
	return {};
}

Result<void> TextureDraft::GenerateChecker(Ursa::UInt32 cellSize, Ursa::Array<Ursa::UInt8, 4> first,
                                           Ursa::Array<Ursa::UInt8, 4> second)
{
	if (!cellSize)
	{
		return Result<void>::Failure("Checker cell size must be positive");
	}
	for (Ursa::UInt32 y = 0; y < height; ++y)
	{
		for (Ursa::UInt32 x = 0; x < width; ++x)
		{
			const auto& color = ((x / cellSize + y / cellSize) % 2) ? second : first;
			const Ursa::Size offset = (static_cast<Ursa::Size>(y) * width + x) * 4;
			for (Ursa::Size channel = 0; channel < 4; ++channel)
			{
				pixels[offset + channel] = color[channel];
			}
		}
	}
	return {};
}

Result<void> TextureDraft::Save(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id) const
{
	auto texture = MaterialTexture::Create(width, height, colorSpace, pixels);
	if (!texture)
	{
		return Result<void>::Failure(texture.error);
	}
	return SaveTextureResource(filesystem, path, id, *texture.value);
}

Result<std::unique_ptr<EditorDocument>> NewTextureDocument(const VirtualPath& path)
{
	Vector<Ursa::UInt8> pixels(8 * 8 * 4, 255);
	auto draft = TextureDraft::Create(8, 8, TextureColorSpace::Srgb, std::move(pixels));
	if (!draft)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(draft.error);
	}
	auto checker = draft.value->GenerateChecker(2, {255, 255, 255, 255}, {50, 50, 50, 255});
	if (!checker)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(checker.error);
	}
	auto document =
	    EditorDocument::Create(ResourceKind::Texture, ResourceId::Generate(), path,
	                           [draft = draft.value](VirtualFileSystem& filesystem, const VirtualPath& target,
	                                                 ResourceId id) { return draft->Save(filesystem, target, id); });
	if (document)
	{
		document.value->SetPayload(draft.value);
		document.value->MarkDirty();
	}
	return document;
}

Result<std::unique_ptr<EditorDocument>> OpenTextureDocument(const VirtualPath& path, const ResourceIndex& index)
{

	auto loaded = LoadTextureResource(index.Filesystem(), path);
	if (!loaded)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(loaded.error);
	}
	auto draft = TextureDraft::Create(loaded.value.texture->GetWidth(), loaded.value.texture->GetHeight(),
	                                  loaded.value.texture->GetColorSpace(), loaded.value.texture->GetPixels());
	if (!draft)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(draft.error);
	}
	auto document =
	    EditorDocument::Create(ResourceKind::Texture, loaded.value.id, path,
	                           [draft = draft.value](VirtualFileSystem& filesystem, const VirtualPath& target,
	                                                 ResourceId id) { return draft->Save(filesystem, target, id); });
	if (document)
	{
		document.value->SetPayload(draft.value);
	}
	return document;
}

Result<bool> DrawTextureInspector(EditorDocument& document, String& diagnostic, bool embedded)
{
	auto draft = document.Payload<TextureDraft>();
	if (!draft)
	{
		return Result<bool>::Failure("Texture document has no texture draft");
	}
	bool changed = false;
	if (!embedded)
	{
		ImGui::Begin("Inspector");
	}
	Icons::Section(Icons::Icon::Texture, "Texture");
	int width = static_cast<int>(draft->GetWidth());
	int height = static_cast<int>(draft->GetHeight());
	if (ImGui::InputInt("Width", &width) && width > 0)
	{
		auto resized = draft->Resize(static_cast<Ursa::UInt32>(width), draft->GetHeight());
		diagnostic = resized ? String{} : resized.error;
		changed |= static_cast<bool>(resized);
	}
	if (ImGui::InputInt("Height", &height) && height > 0)
	{
		auto resized = draft->Resize(draft->GetWidth(), static_cast<Ursa::UInt32>(height));
		diagnostic = resized ? String{} : resized.error;
		changed |= static_cast<bool>(resized);
	}
	int colorSpace = draft->GetColorSpace() == TextureColorSpace::Srgb ? 1 : 0;
	if (ImGui::Combo("Color space", &colorSpace, "Linear\0sRGB\0"))
	{
		auto set = draft->SetColorSpace(colorSpace ? TextureColorSpace::Srgb : TextureColorSpace::Linear);
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	if (Icons::Button("Generate checker"))
	{
		auto generated = draft->GenerateChecker(2, {255, 255, 255, 255}, {50, 50, 50, 255});
		diagnostic = generated ? String{} : generated.error;
		changed |= static_cast<bool>(generated);
	}
	static int selectedX = 0;
	static int selectedY = 0;
	selectedX = std::clamp(selectedX, 0, static_cast<int>(draft->GetWidth()) - 1);
	selectedY = std::clamp(selectedY, 0, static_cast<int>(draft->GetHeight()) - 1);
	ImGui::InputInt("Pixel X", &selectedX);
	ImGui::InputInt("Pixel Y", &selectedY);
	selectedX = std::clamp(selectedX, 0, static_cast<int>(draft->GetWidth()) - 1);
	selectedY = std::clamp(selectedY, 0, static_cast<int>(draft->GetHeight()) - 1);
	const Ursa::Size offset = (static_cast<Ursa::Size>(selectedY) * draft->GetWidth() + selectedX) * 4;
	Ursa::Float32 pixel[4]{};
	for (int channel = 0; channel < 4; ++channel)
	{
		pixel[channel] = draft->GetPixels()[offset + channel] / 255.f;
	}
	if (ImGui::ColorEdit4("RGBA", pixel))
	{
		auto set = draft->SetPixel(static_cast<Ursa::UInt32>(selectedX), static_cast<Ursa::UInt32>(selectedY),
		                           {ToByte(pixel[0]), ToByte(pixel[1]), ToByte(pixel[2]), ToByte(pixel[3])});
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	ImGui::TextUnformatted("Preview");
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const Ursa::Float32 size = 256.f;
	const Ursa::UInt32 columns = std::min<Ursa::UInt32>(draft->GetWidth(), 64);
	const Ursa::UInt32 rows = std::min<Ursa::UInt32>(draft->GetHeight(), 64);
	auto* draw = ImGui::GetWindowDrawList();
	for (Ursa::UInt32 y = 0; y < rows; ++y)
	{
		for (Ursa::UInt32 x = 0; x < columns; ++x)
		{
			const Ursa::UInt32 sourceX = x * draft->GetWidth() / columns;
			const Ursa::UInt32 sourceY = y * draft->GetHeight() / rows;
			const Ursa::Size at = (static_cast<Ursa::Size>(sourceY) * draft->GetWidth() + sourceX) * 4;
			const auto& bytes = draft->GetPixels();
			draw->AddRectFilled({origin.x + x * size / columns, origin.y + y * size / rows},
			                    {origin.x + (x + 1) * size / columns, origin.y + (y + 1) * size / rows},
			                    IM_COL32(bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]));
		}
	}
	ImGui::Dummy({size, size});
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	if (!embedded)
	{
		ImGui::End();
	}
	if (changed)
	{
		document.MarkDirty();
	}
	return Result<bool>::Success(changed);
}
} // namespace Ursa::Editor
