#pragma once

#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Serialization/TextureResource.hpp>

namespace Ursa::Editor
{
class TextureDraft final
{
public:
	static Result<std::shared_ptr<TextureDraft>> Create(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight,
	                                                    TextureColorSpace textureColorSpace,
	                                                    Vector<Ursa::UInt8> texturePixels);

	static Result<std::shared_ptr<TextureDraft>> Load(const std::filesystem::path& path);

	Result<void> ReplacePixels(Ursa::UInt32 newWidth, Ursa::UInt32 newHeight, TextureColorSpace newColorSpace,
	                           Vector<Ursa::UInt8> newPixels);

	Result<void> Resize(Ursa::UInt32 newWidth, Ursa::UInt32 newHeight);

	Result<void> SetColorSpace(TextureColorSpace newColorSpace);

	Result<void> SetPixel(Ursa::UInt32 x, Ursa::UInt32 y, Ursa::Array<Ursa::UInt8, 4> rgba);

	Result<void> GenerateChecker(Ursa::UInt32 cellSize, Ursa::Array<Ursa::UInt8, 4> first,
	                             Ursa::Array<Ursa::UInt8, 4> second);

	Result<void> Save(const std::filesystem::path& path, ResourceId id) const;

	Ursa::UInt32 GetWidth() const
	{
		return width;
	}

	Ursa::UInt32 GetHeight() const
	{
		return height;
	}

	TextureColorSpace GetColorSpace() const
	{
		return colorSpace;
	}

	const Vector<Ursa::UInt8>& GetPixels() const
	{
		return pixels;
	}

private:
	TextureDraft(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight, TextureColorSpace textureColorSpace,
	             Vector<Ursa::UInt8> texturePixels)
	    : width(textureWidth), height(textureHeight), colorSpace(textureColorSpace), pixels(std::move(texturePixels))
	{
	}

	Ursa::UInt32 width;
	Ursa::UInt32 height;
	TextureColorSpace colorSpace;
	Vector<Ursa::UInt8> pixels;
};

Result<std::unique_ptr<EditorDocument>> NewTextureDocument(const VirtualPath& path);

Result<std::unique_ptr<EditorDocument>> OpenTextureDocument(const VirtualPath& path, const ResourceIndex& index);

Result<bool> DrawTextureInspector(EditorDocument& document, String& diagnostic);
} // namespace Ursa::Editor
