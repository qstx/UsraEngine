#include <assets/TextureDraft.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Ursa::Editor
{
namespace
{
constexpr uint64_t maxEditorPixels = 2048ull * 2048ull;

uint8_t ToByte(float value)
{
	return static_cast<uint8_t>(std::round(std::clamp(value, 0.f, 1.f) * 255.f));
}
} // namespace

Result<std::shared_ptr<TextureDraft>> TextureDraft::Create(uint32_t width, uint32_t height,
                                                           TextureColorSpace colorSpace, Vector<uint8_t> pixels)
{
	if (!width || !height || static_cast<uint64_t>(width) * height > maxEditorPixels)
	{
		return Result<std::shared_ptr<TextureDraft>>::Failure("Editor texture dimensions exceed 2048 x 2048");
	}
	auto validated = MaterialTexture::Create(width, height, colorSpace, pixels);
	if (!validated)
	{
		return Result<std::shared_ptr<TextureDraft>>::Failure(validated.error);
	}
	return Result<std::shared_ptr<TextureDraft>>::Success(
	    std::shared_ptr<TextureDraft>(new TextureDraft(width, height, colorSpace, std::move(pixels))));
}

Result<std::shared_ptr<TextureDraft>> TextureDraft::Load(const std::filesystem::path& path)
{
	auto loaded = LoadTextureResource(path);
	if (!loaded)
	{
		return Result<std::shared_ptr<TextureDraft>>::Failure(loaded.error);
	}
	return Create(loaded.value.texture->GetWidth(), loaded.value.texture->GetHeight(),
	              loaded.value.texture->GetColorSpace(), loaded.value.texture->GetPixels());
}

Result<void> TextureDraft::ReplacePixels(uint32_t width, uint32_t height, TextureColorSpace colorSpace,
                                         Vector<uint8_t> pixels)
{
	auto replacement = Create(width, height, colorSpace, std::move(pixels));
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

Result<void> TextureDraft::Resize(uint32_t width, uint32_t height)
{
	if (!width || !height || static_cast<uint64_t>(width) * height > maxEditorPixels)
	{
		return Result<void>::Failure("Editor texture dimensions exceed 2048 x 2048");
	}
	Vector<uint8_t> resized(static_cast<size_t>(width) * height * 4, 255);
	const uint32_t copyWidth = std::min(width, width);
	const uint32_t copyHeight = std::min(height, height);
	for (uint32_t y = 0; y < copyHeight; ++y)
	{
		for (uint32_t x = 0; x < copyWidth; ++x)
		{
			for (uint32_t channel = 0; channel < 4; ++channel)
			{
				resized[(static_cast<size_t>(y) * width + x) * 4 + channel] =
				    pixels[(static_cast<size_t>(y) * width + x) * 4 + channel];
			}
		}
	}
	return ReplacePixels(width, height, colorSpace, std::move(resized));
}

Result<void> TextureDraft::SetColorSpace(TextureColorSpace colorSpace)
{
	if (colorSpace != TextureColorSpace::linear && colorSpace != TextureColorSpace::srgb)
	{
		return Result<void>::Failure("Invalid Editor texture color space");
	}
	colorSpace = colorSpace;
	return {};
}

Result<void> TextureDraft::SetPixel(uint32_t x, uint32_t y, std::array<uint8_t, 4> rgba)
{
	if (x >= width || y >= height)
	{
		return Result<void>::Failure("Texture pixel coordinate is outside its dimensions");
	}
	const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
	for (size_t channel = 0; channel < 4; ++channel)
	{
		pixels[offset + channel] = rgba[channel];
	}
	return {};
}

Result<void> TextureDraft::GenerateChecker(uint32_t cellSize, std::array<uint8_t, 4> first,
                                           std::array<uint8_t, 4> second)
{
	if (!cellSize)
	{
		return Result<void>::Failure("Checker cell size must be positive");
	}
	for (uint32_t y = 0; y < height; ++y)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			const auto& color = ((x / cellSize + y / cellSize) % 2) ? second : first;
			const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
			for (size_t channel = 0; channel < 4; ++channel)
			{
				pixels[offset + channel] = color[channel];
			}
		}
	}
	return {};
}

Result<void> TextureDraft::Save(const std::filesystem::path& path, ResourceId id) const
{
	auto texture = MaterialTexture::Create(width, height, colorSpace, pixels);
	if (!texture)
	{
		return Result<void>::Failure(texture.error);
	}
	return SaveTextureResource(path, id, *texture.value);
}

Result<std::unique_ptr<EditorDocument>> NewTextureDocument(const VirtualPath& path)
{
	Vector<uint8_t> pixels(8 * 8 * 4, 255);
	auto draft = TextureDraft::Create(8, 8, TextureColorSpace::srgb, std::move(pixels));
	if (!draft)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(draft.error);
	}
	auto checker = draft.value->GenerateChecker(2, {255, 255, 255, 255}, {50, 50, 50, 255});
	if (!checker)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(checker.error);
	}
	auto document = EditorDocument::Create(ResourceKind::texture, ResourceId::Generate(), path,
	                                       [draft = draft.value](const std::filesystem::path& target, ResourceId id)
	                                       { return draft->Save(target, id); });
	if (document)
	{
		document.value->SetPayload(draft.value);
		document.value->MarkDirty();
	}
	return document;
}

Result<std::unique_ptr<EditorDocument>> OpenTextureDocument(const VirtualPath& path, const ResourceIndex& index)
{
	auto host = index.Locate(ResourceLocation{ResourceKind::texture, path});
	if (!host)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(host.error);
	}
	auto loaded = LoadTextureResource(host.value);
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
	auto document = EditorDocument::Create(ResourceKind::texture, loaded.value.id, path,
	                                       [draft = draft.value](const std::filesystem::path& target, ResourceId id)
	                                       { return draft->Save(target, id); });
	if (document)
	{
		document.value->SetPayload(draft.value);
	}
	return document;
}

Result<bool> DrawTextureInspector(EditorDocument& document, String& diagnostic)
{
	auto draft = document.Payload<TextureDraft>();
	if (!draft)
	{
		return Result<bool>::Failure("Texture document has no texture draft");
	}
	bool changed = false;
	ImGui::Begin("Inspector");
	int width = static_cast<int>(draft->GetWidth());
	int height = static_cast<int>(draft->GetHeight());
	if (ImGui::InputInt("Width", &width) && width > 0)
	{
		auto resized = draft->Resize(static_cast<uint32_t>(width), draft->GetHeight());
		diagnostic = resized ? String{} : resized.error;
		changed |= static_cast<bool>(resized);
	}
	if (ImGui::InputInt("Height", &height) && height > 0)
	{
		auto resized = draft->Resize(draft->GetWidth(), static_cast<uint32_t>(height));
		diagnostic = resized ? String{} : resized.error;
		changed |= static_cast<bool>(resized);
	}
	int colorSpace = draft->GetColorSpace() == TextureColorSpace::srgb ? 1 : 0;
	if (ImGui::Combo("Color space", &colorSpace, "Linear\0sRGB\0"))
	{
		auto set = draft->SetColorSpace(colorSpace ? TextureColorSpace::srgb : TextureColorSpace::linear);
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	if (ImGui::Button("Generate checker"))
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
	const size_t offset = (static_cast<size_t>(selectedY) * draft->GetWidth() + selectedX) * 4;
	float pixel[4]{};
	for (int channel = 0; channel < 4; ++channel)
	{
		pixel[channel] = draft->GetPixels()[offset + channel] / 255.f;
	}
	if (ImGui::ColorEdit4("RGBA", pixel))
	{
		auto set = draft->SetPixel(static_cast<uint32_t>(selectedX), static_cast<uint32_t>(selectedY),
		                           {ToByte(pixel[0]), ToByte(pixel[1]), ToByte(pixel[2]), ToByte(pixel[3])});
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	ImGui::TextUnformatted("Preview");
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float size = 256.f;
	const uint32_t columns = std::min<uint32_t>(draft->GetWidth(), 64);
	const uint32_t rows = std::min<uint32_t>(draft->GetHeight(), 64);
	auto* draw = ImGui::GetWindowDrawList();
	for (uint32_t y = 0; y < rows; ++y)
	{
		for (uint32_t x = 0; x < columns; ++x)
		{
			const uint32_t sourceX = x * draft->GetWidth() / columns;
			const uint32_t sourceY = y * draft->GetHeight() / rows;
			const size_t at = (static_cast<size_t>(sourceY) * draft->GetWidth() + sourceX) * 4;
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
	ImGui::End();
	if (changed)
	{
		document.MarkDirty();
	}
	return Result<bool>::Success(changed);
}
} // namespace Ursa::Editor
