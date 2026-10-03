#include <assets/TextureDraft.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace ursa::editor {
namespace {
constexpr uint64_t maxEditorPixels = 2048ull * 2048ull;

uint8_t toByte(float value) {
    return static_cast<uint8_t>(std::round(std::clamp(value, 0.f, 1.f) * 255.f));
}
} // namespace

Result<std::shared_ptr<TextureDraft>> TextureDraft::create(uint32_t width, uint32_t height,
                                                            TextureColorSpace colorSpace, Vector<uint8_t> pixels) {
    if (!width || !height || static_cast<uint64_t>(width) * height > maxEditorPixels)
        return Result<std::shared_ptr<TextureDraft>>::failure("Editor texture dimensions exceed 2048 x 2048");
    auto validated = MaterialTexture::create(width, height, colorSpace, pixels);
    if (!validated)
        return Result<std::shared_ptr<TextureDraft>>::failure(validated.error);
    return Result<std::shared_ptr<TextureDraft>>::success(
        std::shared_ptr<TextureDraft>(new TextureDraft(width, height, colorSpace, std::move(pixels))));
}

Result<std::shared_ptr<TextureDraft>> TextureDraft::load(const std::filesystem::path &path) {
    auto loaded = loadTextureResource(path);
    if (!loaded)
        return Result<std::shared_ptr<TextureDraft>>::failure(loaded.error);
    return create(loaded.value.texture->width(), loaded.value.texture->height(),
                  loaded.value.texture->colorSpace(), loaded.value.texture->pixels());
}

Result<void> TextureDraft::replacePixels(uint32_t width, uint32_t height, TextureColorSpace colorSpace,
                                         Vector<uint8_t> pixels) {
    auto replacement = create(width, height, colorSpace, std::move(pixels));
    if (!replacement)
        return Result<void>::failure(replacement.error);
    width_ = replacement.value->width_;
    height_ = replacement.value->height_;
    colorSpace_ = replacement.value->colorSpace_;
    pixels_ = std::move(replacement.value->pixels_);
    return {};
}

Result<void> TextureDraft::resize(uint32_t width, uint32_t height) {
    if (!width || !height || static_cast<uint64_t>(width) * height > maxEditorPixels)
        return Result<void>::failure("Editor texture dimensions exceed 2048 x 2048");
    Vector<uint8_t> resized(static_cast<size_t>(width) * height * 4, 255);
    const uint32_t copyWidth = std::min(width, width_);
    const uint32_t copyHeight = std::min(height, height_);
    for (uint32_t y = 0; y < copyHeight; ++y)
        for (uint32_t x = 0; x < copyWidth; ++x)
            for (uint32_t channel = 0; channel < 4; ++channel)
                resized[(static_cast<size_t>(y) * width + x) * 4 + channel] =
                    pixels_[(static_cast<size_t>(y) * width_ + x) * 4 + channel];
    return replacePixels(width, height, colorSpace_, std::move(resized));
}

Result<void> TextureDraft::setColorSpace(TextureColorSpace colorSpace) {
    if (colorSpace != TextureColorSpace::Linear && colorSpace != TextureColorSpace::Srgb)
        return Result<void>::failure("Invalid Editor texture color space");
    colorSpace_ = colorSpace;
    return {};
}

Result<void> TextureDraft::setPixel(uint32_t x, uint32_t y, std::array<uint8_t, 4> rgba) {
    if (x >= width_ || y >= height_)
        return Result<void>::failure("Texture pixel coordinate is outside its dimensions");
    const size_t offset = (static_cast<size_t>(y) * width_ + x) * 4;
    for (size_t channel = 0; channel < 4; ++channel)
        pixels_[offset + channel] = rgba[channel];
    return {};
}

Result<void> TextureDraft::generateChecker(uint32_t cellSize, std::array<uint8_t, 4> first,
                                            std::array<uint8_t, 4> second) {
    if (!cellSize)
        return Result<void>::failure("Checker cell size must be positive");
    for (uint32_t y = 0; y < height_; ++y)
        for (uint32_t x = 0; x < width_; ++x) {
            const auto &color = ((x / cellSize + y / cellSize) % 2) ? second : first;
            const size_t offset = (static_cast<size_t>(y) * width_ + x) * 4;
            for (size_t channel = 0; channel < 4; ++channel)
                pixels_[offset + channel] = color[channel];
        }
    return {};
}

Result<void> TextureDraft::save(const std::filesystem::path &path, ResourceId id) const {
    auto texture = MaterialTexture::create(width_, height_, colorSpace_, pixels_);
    if (!texture)
        return Result<void>::failure(texture.error);
    return saveTextureResource(path, id, *texture.value);
}

Result<std::unique_ptr<EditorDocument>> newTextureDocument(const VirtualPath &path) {
    Vector<uint8_t> pixels(8 * 8 * 4, 255);
    auto draft = TextureDraft::create(8, 8, TextureColorSpace::Srgb, std::move(pixels));
    if (!draft)
        return Result<std::unique_ptr<EditorDocument>>::failure(draft.error);
    auto checker = draft.value->generateChecker(2, {255, 255, 255, 255}, {50, 50, 50, 255});
    if (!checker)
        return Result<std::unique_ptr<EditorDocument>>::failure(checker.error);
    auto document = EditorDocument::create(ResourceKind::Texture, ResourceId::generate(), path,
                                            [draft = draft.value](const std::filesystem::path &target, ResourceId id) {
                                                return draft->save(target, id);
                                            });
    if (document) {
        document.value->setPayload(draft.value);
        document.value->markDirty();
    }
    return document;
}

Result<std::unique_ptr<EditorDocument>> openTextureDocument(const VirtualPath &path, const ResourceIndex &index) {
    auto host = index.locate(ResourceLocation{ResourceKind::Texture, path});
    if (!host)
        return Result<std::unique_ptr<EditorDocument>>::failure(host.error);
    auto loaded = loadTextureResource(host.value);
    if (!loaded)
        return Result<std::unique_ptr<EditorDocument>>::failure(loaded.error);
    auto draft = TextureDraft::create(loaded.value.texture->width(), loaded.value.texture->height(),
                                      loaded.value.texture->colorSpace(), loaded.value.texture->pixels());
    if (!draft)
        return Result<std::unique_ptr<EditorDocument>>::failure(draft.error);
    auto document = EditorDocument::create(ResourceKind::Texture, loaded.value.id, path,
                                            [draft = draft.value](const std::filesystem::path &target, ResourceId id) {
                                                return draft->save(target, id);
                                            });
    if (document)
        document.value->setPayload(draft.value);
    return document;
}

Result<bool> drawTextureInspector(EditorDocument &document, String &diagnostic) {
    auto draft = document.payload<TextureDraft>();
    if (!draft)
        return Result<bool>::failure("Texture document has no texture draft");
    bool changed = false;
    ImGui::Begin("Inspector");
    int width = static_cast<int>(draft->width());
    int height = static_cast<int>(draft->height());
    if (ImGui::InputInt("Width", &width) && width > 0) {
        auto resized = draft->resize(static_cast<uint32_t>(width), draft->height());
        diagnostic = resized ? String{} : resized.error;
        changed |= static_cast<bool>(resized);
    }
    if (ImGui::InputInt("Height", &height) && height > 0) {
        auto resized = draft->resize(draft->width(), static_cast<uint32_t>(height));
        diagnostic = resized ? String{} : resized.error;
        changed |= static_cast<bool>(resized);
    }
    int colorSpace = draft->colorSpace() == TextureColorSpace::Srgb ? 1 : 0;
    if (ImGui::Combo("Color space", &colorSpace, "Linear\0sRGB\0")) {
        auto set = draft->setColorSpace(colorSpace ? TextureColorSpace::Srgb : TextureColorSpace::Linear);
        diagnostic = set ? String{} : set.error;
        changed |= static_cast<bool>(set);
    }
    if (ImGui::Button("Generate checker")) {
        auto generated = draft->generateChecker(2, {255, 255, 255, 255}, {50, 50, 50, 255});
        diagnostic = generated ? String{} : generated.error;
        changed |= static_cast<bool>(generated);
    }
    static int selectedX = 0;
    static int selectedY = 0;
    selectedX = std::clamp(selectedX, 0, static_cast<int>(draft->width()) - 1);
    selectedY = std::clamp(selectedY, 0, static_cast<int>(draft->height()) - 1);
    ImGui::InputInt("Pixel X", &selectedX);
    ImGui::InputInt("Pixel Y", &selectedY);
    selectedX = std::clamp(selectedX, 0, static_cast<int>(draft->width()) - 1);
    selectedY = std::clamp(selectedY, 0, static_cast<int>(draft->height()) - 1);
    const size_t offset = (static_cast<size_t>(selectedY) * draft->width() + selectedX) * 4;
    float pixel[4]{};
    for (int channel = 0; channel < 4; ++channel)
        pixel[channel] = draft->pixels()[offset + channel] / 255.f;
    if (ImGui::ColorEdit4("RGBA", pixel)) {
        auto set = draft->setPixel(static_cast<uint32_t>(selectedX), static_cast<uint32_t>(selectedY),
                                    {toByte(pixel[0]), toByte(pixel[1]), toByte(pixel[2]), toByte(pixel[3])});
        diagnostic = set ? String{} : set.error;
        changed |= static_cast<bool>(set);
    }
    ImGui::TextUnformatted("Preview");
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float size = 256.f;
    const uint32_t columns = std::min<uint32_t>(draft->width(), 64);
    const uint32_t rows = std::min<uint32_t>(draft->height(), 64);
    auto *draw = ImGui::GetWindowDrawList();
    for (uint32_t y = 0; y < rows; ++y)
        for (uint32_t x = 0; x < columns; ++x) {
            const uint32_t sourceX = x * draft->width() / columns;
            const uint32_t sourceY = y * draft->height() / rows;
            const size_t at = (static_cast<size_t>(sourceY) * draft->width() + sourceX) * 4;
            const auto &bytes = draft->pixels();
            draw->AddRectFilled({origin.x + x * size / columns, origin.y + y * size / rows},
                                {origin.x + (x + 1) * size / columns, origin.y + (y + 1) * size / rows},
                                IM_COL32(bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]));
        }
    ImGui::Dummy({size, size});
    if (!diagnostic.empty())
        ImGui::TextWrapped("%s", diagnostic.c_str());
    ImGui::End();
    if (changed)
        document.markDirty();
    return Result<bool>::success(changed);
}
} // namespace ursa::editor
