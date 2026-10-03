#include <assets/MaterialDraft.hpp>
#include <assets/MaterialPreview.hpp>
#include <serialization/TextureResource.hpp>
#include <core/StringInterop.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace ursa;
using namespace ursa::rhi;

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

std::string bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
} // namespace

int main() {
    const auto root = std::filesystem::current_path() /
                      ("material_draft_" + toStdString(ResourceId::generate().toString()));
    try {
        std::filesystem::create_directories(root / "assets");
        const auto textureId = ResourceId::generate();
        auto texture = take(MaterialTexture::create(2, 2, TextureColorSpace::Srgb,
                                                   {255, 80, 20, 255, 255, 80, 20, 255,
                                                    255, 80, 20, 255, 255, 80, 20, 255}));
        done(saveTextureResource(root / "assets" / "shared.ursatex", textureId, *texture));
        auto workspace = take(editor::ProjectWorkspace::open(root));
        auto documents = take(editor::EditorDocuments::create(*workspace));
        const auto firstPath = root / "assets" / "first.ursamat";
        auto first = take(editor::newMaterialDocument(take(VirtualPath::parse("prj:first.ursamat")),
                                                      MaterialType::Unlit, *workspace));
        const auto firstId = first->id();
        auto firstDraft = first->payload<editor::MaterialDraft>();
        done(firstDraft->setTextureId(MaterialTextureSlot::BaseColor, textureId, documents->session()));
        expect(!firstDraft->setTextureId(MaterialTextureSlot::Normal, textureId, documents->session()),
               "Unlit material accepted an unsupported texture slot");
        done(firstDraft->material().setBaseColor({0.8f, 0.6f, 0.4f, 1.f}));
        done(documents->requestOpen(std::move(first)));
        done(documents->saveCurrent());
        const auto firstBytes = bytes(firstPath);
        expect(!firstDraft->material().setMetallic(-1.f), "Invalid Unlit metallic value was accepted");
        expect(bytes(firstPath) == firstBytes, "Invalid material edit changed a saved resource");

        const auto secondPath = root / "assets" / "second.ursamat";
        auto second = take(editor::newMaterialDocument(take(VirtualPath::parse("prj:second.ursamat")),
                                                       MaterialType::PbrMetallicRoughness, *workspace));
        const auto secondId = second->id();
        auto secondDraft = second->payload<editor::MaterialDraft>();
        done(secondDraft->setTextureId(MaterialTextureSlot::BaseColor, textureId, documents->session()));
        done(secondDraft->material().setMetallic(0.7f));
        done(secondDraft->material().setRoughness(0.3f));
        done(secondDraft->material().setAlphaMode(AlphaMode::Mask, 0.4f));
        done(secondDraft->material().setSampler({false, true}));
        done(documents->requestOpen(std::move(second)));
        done(documents->saveCurrent());
        auto reopenedFirst = take(editor::openMaterialDocument(
            take(VirtualPath::parse("prj:first.ursamat")), documents->session(), *workspace));
        auto reopenedSecond = take(editor::openMaterialDocument(
            take(VirtualPath::parse("prj:second.ursamat")), documents->session(), *workspace));
        auto firstLoaded = reopenedFirst->payload<editor::MaterialDraft>();
        auto secondLoaded = reopenedSecond->payload<editor::MaterialDraft>();
        expect(reopenedFirst->id() == firstId && reopenedSecond->id() == secondId,
               "Material resource IDs changed after reopen");
        expect(firstLoaded->textureId(MaterialTextureSlot::BaseColor) == textureId &&
                   secondLoaded->textureId(MaterialTextureSlot::BaseColor) == textureId,
               "Shared material texture reference ID changed");
        expect(firstLoaded->material().texture(MaterialTextureSlot::BaseColor) ==
                   secondLoaded->material().texture(MaterialTextureSlot::BaseColor),
               "Shared texture did not resolve to the same resource-session object");
        expect(secondLoaded->material().parameters().roughness == 0.3f &&
                   secondLoaded->material().parameters().sampler.linear == false &&
                   secondLoaded->material().parameters().alphaMode == AlphaMode::Mask,
               "PBR parameters did not reopen");

        DeviceDesc desc;
#ifndef NDEBUG
        desc.validation = true;
#endif
        auto device = take(createDevice(desc));
        auto gui = take(editor::ImGuiBridge::create(device));
        auto preview = take(editor::MaterialPreview::create(device, *gui, 64));
        done(preview->render(secondLoaded->materialPtr()));
        auto readback = take(device->createBuffer({64 * 64 * 4, BufferUsage::TransferDestination, Memory::Readback}));
        auto command = take(device->createCommandList());
        done(command->begin());
        done(command->barrier(preview->colorTexture(), ResourceState::ShaderRead, ResourceState::TransferSource));
        done(command->barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
        done(command->copyTextureToBuffer(preview->colorTexture(), readback));
        done(command->end());
        done(take(device->submit(command))->wait());
        Array<uint8_t, 64 * 64 * 4> pixels{};
        done(readback->read(0, pixels.data(), pixels.size()));
        const size_t center = (32 * 64 + 32) * 4;
        expect(pixels[center] > 30 || pixels[center + 1] > 30,
               "Material 3D preview did not render the cube");
        done(device->waitIdle());
        preview.reset();
        gui.reset();
        device.reset();
        std::filesystem::remove_all(root);
        std::cout << "Editor material authoring test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
