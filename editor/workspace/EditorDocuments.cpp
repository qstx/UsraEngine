#include <workspace/EditorDocuments.hpp>
#include <core/Log.hpp>
#include <serialization/AtomicFile.hpp>
#include <serialization/ResourceFile.hpp>
#include <core/StringInterop.hpp>
#include <imgui.h>
#include <fstream>
#include <iterator>
#include <string>

namespace ursa::editor {
namespace {
const char *extensionFor(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::Texture: return ".ursatex";
    case ResourceKind::Material: return ".ursamat";
    case ResourceKind::Scene: return ".ursascene";
    case ResourceKind::Prefab: return ".ursaprefab";
    }
    return "";
}

String virtualFileName(const VirtualPath &path) {
    const auto slash = path.relative.rfind('/');
    return slash == String::npos ? path.relative : path.relative.substr(slash + 1);
}

Result<void> restoreFile(const std::filesystem::path &path, bool existed, const std::string &original) {
    if (!existed) {
        std::error_code error;
        std::filesystem::remove(path, error);
        return error ? Result<void>::failure("Cannot remove failed new asset: " + pathToUrsaString(path))
                     : Result<void>{};
    }
    return writeResourceAtomically(path, [&](std::ostream &output) {
        output.write(original.data(), static_cast<std::streamsize>(original.size()));
        return output ? Result<void>{} : Result<void>::failure("Cannot restore old asset bytes");
    });
}
} // namespace

Result<std::unique_ptr<EditorDocument>> EditorDocument::create(ResourceKind kind, ResourceId id, VirtualPath path,
                                                               DocumentWriter writer) {
    const auto matched = resourceKindForPath(path);
    if (!id || !writer || !matched || matched.value != kind)
        return Result<std::unique_ptr<EditorDocument>>::failure("Invalid Editor document ID, path, or writer");
    return Result<std::unique_ptr<EditorDocument>>::success(
        std::unique_ptr<EditorDocument>(new EditorDocument(kind, id, std::move(path), std::move(writer))));
}

Result<std::unique_ptr<EditorDocuments>> EditorDocuments::create(ProjectWorkspace &workspace) {
    auto session = ResourceSession::open(workspace.index().mounts());
    if (!session)
        return Result<std::unique_ptr<EditorDocuments>>::failure(session.error);
    auto documents = std::unique_ptr<EditorDocuments>(new EditorDocuments(workspace));
    documents->sessions_.push_back(std::move(session.value));
    return Result<std::unique_ptr<EditorDocuments>>::success(std::move(documents));
}

Result<void> EditorDocuments::requestOpen(std::unique_ptr<EditorDocument> document) {
    if (!document)
        return Result<void>::failure("Cannot open an empty Editor document");
    if (promptPending_)
        return Result<void>::failure("Resolve the unsaved changes prompt first");
    auto target = workspace_.writablePath(document->path());
    if (!target)
        return Result<void>::failure(target.error);
    pending_ = std::move(document);
    pendingClose_ = false;
    if (current_ && current_->dirty())
        promptPending_ = true;
    else
        finishPending();
    return {};
}

Result<void> EditorDocuments::requestClose() {
    if (promptPending_)
        return Result<void>::failure("Resolve the unsaved changes prompt first");
    pending_.reset();
    pendingClose_ = true;
    if (current_ && current_->dirty())
        promptPending_ = true;
    else
        finishPending();
    return {};
}

void EditorDocuments::finishPending() {
    if (pendingClose_)
        current_.reset();
    else
        current_ = std::move(pending_);
    pendingClose_ = false;
    promptPending_ = false;
    lastError_.clear();
}

Result<void> EditorDocuments::resolvePrompt(UnsavedChoice choice) {
    if (!promptPending_)
        return Result<void>::failure("No unsaved changes prompt is active");
    if (choice == UnsavedChoice::Cancel) {
        pending_.reset();
        pendingClose_ = false;
        promptPending_ = false;
        return {};
    }
    if (choice == UnsavedChoice::Save) {
        auto saved = saveCurrent();
        if (!saved)
            return saved;
    }
    finishPending();
    return {};
}

Result<void> EditorDocuments::saveCurrent() {
    if (saveBlocked_)
        return Result<void>::failure("Saving is disabled while Play mode is active");
    if (!current_)
        return Result<void>::failure("No Editor document is open");
    auto target = workspace_.writablePath(current_->path());
    if (!target)
        return Result<void>::failure(target.error);
    if (target.value.extension() != extensionFor(current_->kind()))
        return Result<void>::failure("Document path has the wrong resource extension");
    auto indexed = workspace_.index().resolve(current_->id());
    if (indexed && indexed.value.path != current_->path())
        return Result<void>::failure("Document ID is already used by " + indexed.value.path.toString());
    std::error_code fileError;
    const bool existed = std::filesystem::exists(target.value, fileError);
    if (fileError)
        return Result<void>::failure("Cannot inspect document destination");
    std::string original;
    if (existed) {
        std::ifstream input(target.value, std::ios::binary);
        if (!input)
            return Result<void>::failure("Cannot read existing document before save");
        original.assign(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
        if (!input.eof() && input.fail())
            return Result<void>::failure("Cannot back up existing document before save");
    }
    const auto stage = target.value.parent_path() /
                       (target.value.filename().string() + ".editor-stage-" +
                        toStdString(current_->id().toString()) + extensionFor(current_->kind()));
    std::filesystem::remove(stage, fileError);
    if (fileError)
        return Result<void>::failure("Cannot clear previous Editor staging file");
    auto written = current_->writer_(stage, current_->id());
    if (!written) {
        std::filesystem::remove(stage, fileError);
        lastError_ = written.error;
        URSA_LOG_ERROR("Editor", "Cannot save asset " + current_->path().toString() + ": " + lastError_);
        return written;
    }
    auto header = readResourceHeader(stage);
    if (!header || header.value.id != current_->id() || header.value.kind != current_->kind()) {
        std::filesystem::remove(stage, fileError);
        lastError_ = header ? String("Editor writer produced the wrong resource ID or kind") : header.error;
        return Result<void>::failure(lastError_);
    }
    written = writeResourceAtomically(target.value, [&](std::ostream &output) {
        std::ifstream input(stage, std::ios::binary);
        if (!input)
            return Result<void>::failure("Cannot open staged Editor asset");
        output << input.rdbuf();
        return output && !input.bad() ? Result<void>{}
                                      : Result<void>::failure("Cannot copy staged Editor asset");
    });
    std::filesystem::remove(stage, fileError);
    if (!written) {
        lastError_ = written.error;
        return written;
    }
    auto refreshed = workspace_.refresh();
    auto session = refreshed ? ResourceSession::open(workspace_.index().mounts())
                             : Result<std::unique_ptr<ResourceSession>>::failure(refreshed.error);
    if (!session) {
        auto restored = restoreFile(target.value, existed, original);
        workspace_.refresh();
        lastError_ = session.error;
        if (!restored)
            lastError_ += "; rollback failed: " + restored.error;
        return Result<void>::failure(lastError_);
    }
    sessions_.push_back(std::move(session.value));
    current_->dirty_ = false;
    lastError_.clear();
    URSA_LOG_INFO("Editor", "Saved asset " + current_->path().toString());
    return {};
}

void EditorDocuments::drawUnsavedPrompt() {
    if (!promptPending_)
        return;
    if (!ImGui::IsPopupOpen("Unsaved changes"))
        ImGui::OpenPopup("Unsaved changes");
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const String name = current_ ? virtualFileName(current_->path()) : String("document");
    ImGui::Text("Save changes to %s?", name.c_str());
    if (!lastError_.empty())
        ImGui::TextWrapped("%s", lastError_.c_str());
    if (ImGui::Button("Save"))
        resolvePrompt(UnsavedChoice::Save);
    ImGui::SameLine();
    if (ImGui::Button("Discard"))
        resolvePrompt(UnsavedChoice::Discard);
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        resolvePrompt(UnsavedChoice::Cancel);
    ImGui::EndPopup();
}
} // namespace ursa::editor
