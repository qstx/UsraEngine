#pragma once

#include <workspace/EditorDocuments.hpp>
#include <serialization/TextureResource.hpp>
#include <array>

namespace ursa::editor {
class TextureDraft final {
public:
    static Result<std::shared_ptr<TextureDraft>> create(uint32_t width, uint32_t height,
                                                         TextureColorSpace colorSpace, Vector<uint8_t> pixels);

    static Result<std::shared_ptr<TextureDraft>> load(const std::filesystem::path &path);

    Result<void> replacePixels(uint32_t width, uint32_t height, TextureColorSpace colorSpace,
                                Vector<uint8_t> pixels);

    Result<void> resize(uint32_t width, uint32_t height);

    Result<void> setColorSpace(TextureColorSpace colorSpace);

    Result<void> setPixel(uint32_t x, uint32_t y, std::array<uint8_t, 4> rgba);

    Result<void> generateChecker(uint32_t cellSize, std::array<uint8_t, 4> first,
                                  std::array<uint8_t, 4> second);

    Result<void> save(const std::filesystem::path &path, ResourceId id) const;

    uint32_t width() const {
        return width_;
    }

    uint32_t height() const {
        return height_;
    }

    TextureColorSpace colorSpace() const {
        return colorSpace_;
    }

    const Vector<uint8_t> &pixels() const {
        return pixels_;
    }

private:
    TextureDraft(uint32_t width, uint32_t height, TextureColorSpace colorSpace, Vector<uint8_t> pixels)
        : width_(width), height_(height), colorSpace_(colorSpace), pixels_(std::move(pixels)) {
    }

    uint32_t width_;
    uint32_t height_;
    TextureColorSpace colorSpace_;
    Vector<uint8_t> pixels_;
};

Result<std::unique_ptr<EditorDocument>> newTextureDocument(const VirtualPath &path);

Result<std::unique_ptr<EditorDocument>> openTextureDocument(const VirtualPath &path, const ResourceIndex &index);

Result<bool> drawTextureInspector(EditorDocument &document, String &diagnostic);
} // namespace ursa::editor
