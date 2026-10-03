#pragma once

#include <workspace/EditorDocuments.hpp>
#include <serialization/MaterialResource.hpp>
#include <array>

namespace ursa::editor {
class MaterialDraft final {
public:
    explicit MaterialDraft(MaterialType type) : material_(std::make_shared<MaterialInstance>(type)) {
    }

    static Result<std::shared_ptr<MaterialDraft>> load(const std::filesystem::path &path, ResourceSession &session);

    Result<void> setTextureId(MaterialTextureSlot slot, ResourceId id, ResourceSession &session);

    Result<void> save(const std::filesystem::path &path, ResourceId id, const ResourceIndex &index) const;

    MaterialInstance &material() {
        return *material_;
    }

    std::shared_ptr<MaterialInstance> materialPtr() const {
        return material_;
    }

    ResourceId textureId(MaterialTextureSlot slot) const;

private:
    std::shared_ptr<MaterialInstance> material_;
    Array<ResourceId, 5> textureIds_{};
};

Result<std::unique_ptr<EditorDocument>> newMaterialDocument(const VirtualPath &path,
                                                             MaterialType type, ProjectWorkspace &workspace);

Result<std::unique_ptr<EditorDocument>> openMaterialDocument(const VirtualPath &path,
                                                              ResourceSession &session,
                                                              ProjectWorkspace &workspace);

Result<bool> drawMaterialInspector(EditorDocument &document, EditorDocuments &documents,
                                    const ProjectWorkspace &workspace, String &diagnostic);
} // namespace ursa::editor
