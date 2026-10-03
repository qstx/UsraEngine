#include <workspace/EditorDocuments.hpp>
#include <serialization/MaterialResource.hpp>
#include <core/StringInterop.hpp>
#include <core/Log.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
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

std::string bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
} // namespace

int main() {
    const auto root = std::filesystem::current_path() /
                      ("editor_documents_" + toStdString(ResourceId::generate().toString()));
    const auto logPath = root.parent_path() / (root.filename().string() + ".log");
    try {
        std::filesystem::create_directories(root / "assets");
        LogConfig config;
        config.filePath = logPath;
        config.level = LogLevel::Debug;
        done(Log::initialize(root / "EditorDocumentsTests.exe", config));
        const auto firstPath = root / "assets" / "first.ursamat";
        const auto firstId = ResourceId::generate();
        MaterialInstance original(MaterialType::Unlit);
        done(saveMaterialResource(firstPath, firstId, original, {}));
        const std::string oldBytes = bytes(firstPath);
        auto workspace = take(editor::ProjectWorkspace::open(root));
        auto documents = take(editor::EditorDocuments::create(*workspace));
        auto edited = std::make_shared<MaterialInstance>(MaterialType::Unlit);
        done(edited->setBaseColor({0.2f, 0.7f, 0.3f, 1.f}));
        bool failWrite = true;
        const editor::DocumentWriter writer = [edited, &failWrite](const std::filesystem::path &path, ResourceId id) {
            if (failWrite)
                return Result<void>::failure("simulated material save failure");
            return saveMaterialResource(path, id, *edited, {});
        };
        auto first = take(editor::EditorDocument::create(
            ResourceKind::Material, firstId, take(VirtualPath::parse("prj:first.ursamat")), writer));
        done(documents->requestOpen(std::move(first)));
        documents->current()->markDirty();
        const auto secondId = ResourceId::generate();
        auto makeSecond = [&] {
            return take(editor::EditorDocument::create(
                ResourceKind::Material, secondId, take(VirtualPath::parse("prj:second.ursamat")),
                [edited](const std::filesystem::path &path, ResourceId id) {
                    return saveMaterialResource(path, id, *edited, {});
                }));
        };
        done(documents->requestOpen(makeSecond()));
        expect(documents->promptPending(), "Switching a dirty document did not request a choice");
        done(documents->resolvePrompt(editor::UnsavedChoice::Cancel));
        expect(documents->current() && documents->current()->id() == firstId && documents->current()->dirty(),
               "Cancel did not preserve the dirty document");
        done(documents->requestClose());
        expect(!documents->resolvePrompt(editor::UnsavedChoice::Save), "Failed save was accepted");
        expect(documents->promptPending() && documents->current() && documents->current()->dirty(),
               "Failed save dropped pending changes");
        expect(bytes(firstPath) == oldBytes, "Failed save modified the existing file");
        expect(bytes(logPath).find("simulated material save failure") != std::string::npos,
               "Failed asset save did not record a diagnostic");
        failWrite = false;
        done(documents->resolvePrompt(editor::UnsavedChoice::Cancel));
        done(documents->saveCurrent());
        expect(!documents->current()->dirty() && documents->current()->id() == firstId,
               "Successful save changed the ID or left the document dirty");
        expect(static_cast<bool>(documents->session().index().resolve(firstId)),
               "Session was not refreshed after save");
        auto loaded = take(loadMaterialResource(firstPath, {}));
        expect(loaded.id == firstId && loaded.material->parameters().baseColor.green > 0.6f,
               "Saved material did not reopen with edited values");
        documents->current()->markDirty();
        done(documents->requestOpen(makeSecond()));
        done(documents->resolvePrompt(editor::UnsavedChoice::Discard));
        expect(documents->current() && documents->current()->id() == secondId &&
                   !std::filesystem::exists(root / "assets" / "second.ursamat"),
               "Discard did not switch documents without writing the old one");
        done(documents->saveCurrent());
        expect(static_cast<bool>(documents->session().index().resolve(secondId)), "New document ID was not indexed");
        Log::shutdown();
        expect(bytes(logPath).find("Saved asset") != std::string::npos,
               "Successful asset save did not record its path");
        std::filesystem::remove_all(root);
        std::filesystem::remove(logPath);
        std::cout << "Editor document safety test passed\n";
        return 0;
    } catch (const std::exception &error) {
        Log::shutdown();
        std::filesystem::remove_all(root);
        std::filesystem::remove(logPath);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
