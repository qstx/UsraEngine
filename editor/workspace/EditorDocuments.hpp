#pragma once

#include <workspace/ProjectWorkspace.hpp>
#include <serialization/ResourceSession.hpp>
#include <vfs/VirtualPath.hpp>
#include <functional>
#include <memory>
#include <typeindex>

namespace ursa::editor {
using DocumentWriter = std::function<Result<void>(const std::filesystem::path &, ResourceId)>;

class EditorDocument final {
public:
    static Result<std::unique_ptr<EditorDocument>> create(ResourceKind kind, ResourceId id, VirtualPath path,
                                                          DocumentWriter writer);

    ResourceKind kind() const {
        return kind_;
    }

    ResourceId id() const {
        return id_;
    }

    const VirtualPath &path() const {
        return path_;
    }

    void setPath(VirtualPath path) {
        path_ = std::move(path);
        dirty_ = true;
    }

    bool dirty() const {
        return dirty_;
    }

    void markDirty() {
        dirty_ = true;
    }

    template <class T> void setPayload(std::shared_ptr<T> value) {
        payload_ = std::move(value);
        payloadType_ = std::type_index(typeid(T));
    }

    template <class T> std::shared_ptr<T> payload() const {
        return payloadType_ == std::type_index(typeid(T)) ? std::static_pointer_cast<T>(payload_) : nullptr;
    }

private:
    EditorDocument(ResourceKind kind, ResourceId id, VirtualPath path, DocumentWriter writer)
        : kind_(kind), id_(id), path_(std::move(path)), writer_(std::move(writer)) {
    }

    friend class EditorDocuments;

    ResourceKind kind_;
    ResourceId id_;
    VirtualPath path_;
    DocumentWriter writer_;
    bool dirty_ = false;
    std::shared_ptr<void> payload_;
    std::type_index payloadType_{typeid(void)};
};

enum class UnsavedChoice { Save, Discard, Cancel };

class EditorDocuments final {
public:
    static Result<std::unique_ptr<EditorDocuments>> create(ProjectWorkspace &workspace);

    Result<void> requestOpen(std::unique_ptr<EditorDocument> document);

    Result<void> requestClose();

    Result<void> resolvePrompt(UnsavedChoice choice);

    Result<void> saveCurrent();

    void setSaveBlocked(bool blocked) {
        saveBlocked_ = blocked;
    }

    void drawUnsavedPrompt();

    bool promptPending() const {
        return promptPending_;
    }

    EditorDocument *current() const {
        return current_.get();
    }

    ResourceSession &session() const {
        return *sessions_.back();
    }

    const String &lastError() const {
        return lastError_;
    }

private:
    explicit EditorDocuments(ProjectWorkspace &workspace) : workspace_(workspace) {
    }

    void finishPending();

    ProjectWorkspace &workspace_;
    std::unique_ptr<EditorDocument> current_;
    std::unique_ptr<EditorDocument> pending_;
    bool pendingClose_ = false;
    bool promptPending_ = false;
    bool saveBlocked_ = false;
    String lastError_;
    Vector<std::unique_ptr<ResourceSession>> sessions_;
};
} // namespace ursa::editor
