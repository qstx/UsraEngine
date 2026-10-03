#pragma once

#include <workspace/EditorDocuments.hpp>
#include <serialization/TextureResource.hpp>
#include <array>

namespace Ursa::Editor
{
class TextureDraft final
{
public:
	static Result<std::shared_ptr<TextureDraft>> Create(uint32_t width, uint32_t height, TextureColorSpace colorSpace,
	                                                    Vector<uint8_t> pixels);

	static Result<std::shared_ptr<TextureDraft>> Load(const std::filesystem::path& path);

	Result<void> ReplacePixels(uint32_t width, uint32_t height, TextureColorSpace colorSpace, Vector<uint8_t> pixels);

	Result<void> Resize(uint32_t width, uint32_t height);

	Result<void> SetColorSpace(TextureColorSpace colorSpace);

	Result<void> SetPixel(uint32_t x, uint32_t y, std::array<uint8_t, 4> rgba);

	Result<void> GenerateChecker(uint32_t cellSize, std::array<uint8_t, 4> first, std::array<uint8_t, 4> second);

	Result<void> Save(const std::filesystem::path& path, ResourceId id) const;

	uint32_t GetWidth() const
	{
		return width;
	}

	uint32_t GetHeight() const
	{
		return height;
	}

	TextureColorSpace GetColorSpace() const
	{
		return colorSpace;
	}

	const Vector<uint8_t>& GetPixels() const
	{
		return pixels;
	}

private:
	TextureDraft(uint32_t width, uint32_t height, TextureColorSpace colorSpace, Vector<uint8_t> pixels)
	    : width(width), height(height), colorSpace(colorSpace), pixels(std::move(pixels))
	{
	}

	uint32_t width;
	uint32_t height;
	TextureColorSpace colorSpace;
	Vector<uint8_t> pixels;
};

Result<std::unique_ptr<EditorDocument>> NewTextureDocument(const VirtualPath& path);

Result<std::unique_ptr<EditorDocument>> OpenTextureDocument(const VirtualPath& path, const ResourceIndex& index);

Result<bool> DrawTextureInspector(EditorDocument& document, String& diagnostic);
} // namespace Ursa::Editor
