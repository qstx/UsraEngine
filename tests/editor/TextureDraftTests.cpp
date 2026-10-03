#include <assets/TextureDraft.hpp>
#include <core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ursa;

template <class T> T take(Result<T> value) {
    if (!value)
        throw std::runtime_error(toStdString(value.error));
    return std::move(value.value);
}

void done(Result<void> value) {
    if (!value)
        throw std::runtime_error(toStdString(value.error));
}

void expect(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    const auto root = std::filesystem::current_path() /
                      ("texture_draft_" + toStdString(ResourceId::generate().toString()));
    try {
        std::filesystem::create_directories(root / "assets");
        auto draft = take(editor::TextureDraft::create(
            2, 2, TextureColorSpace::Srgb,
            {10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255}));
        const auto before = draft->pixels();
        expect(!draft->replacePixels(2, 2, TextureColorSpace::Srgb, {1, 2, 3}),
               "Invalid pixel count was accepted");
        expect(draft->pixels() == before, "Invalid pixel count modified the edit draft");
        done(draft->setPixel(1, 0, {201, 202, 203, 204}));
        const auto path = root / "assets" / "checker.ursatex";
        const auto id = ResourceId::generate();
        done(draft->save(path, id));
        auto loaded = take(loadTextureResource(path));
        expect(loaded.id == id && loaded.texture->width() == 2 && loaded.texture->height() == 2 &&
                   loaded.texture->colorSpace() == TextureColorSpace::Srgb &&
                   loaded.texture->pixels() == draft->pixels(),
               "Non-default texture did not round-trip");
        auto workspace = take(editor::ProjectWorkspace::open(root));
        auto reopened = take(editor::openTextureDocument(take(VirtualPath::parse("prj:checker.ursatex")),
                                                         workspace->index()));
        expect(reopened->id() == id && reopened->payload<editor::TextureDraft>()->pixels() == draft->pixels(),
               "Editor texture document did not reopen with stable ID and pixels");
        auto documents = take(editor::EditorDocuments::create(*workspace));
        auto created = take(editor::newTextureDocument(take(VirtualPath::parse("prj:created.ursatex"))));
        const auto createdId = created->id();
        done(documents->requestOpen(std::move(created)));
        done(documents->saveCurrent());
        expect(take(loadTextureResource(root / "assets" / "created.ursatex")).id == createdId,
               "New procedural texture did not retain its generated ID");
        std::filesystem::remove_all(root);
        std::cout << "Editor texture authoring test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
