#pragma once

#include <Workspace/EditorDocuments.hpp>
#include <Serialization/MaterialResource.hpp>
#include <array>

namespace Ursa::Editor
{
class MaterialDraft final
{
public:
	explicit MaterialDraft(MaterialType type) : material(std::make_shared<MaterialInstance>(type))
	{
	}

	static Result<std::shared_ptr<MaterialDraft>> Load(const std::filesystem::path& path, ResourceSession& session);

	Result<void> SetTextureId(MaterialTextureSlot slot, ResourceId id, ResourceSession& session);

	Result<void> Save(const std::filesystem::path& path, ResourceId id, const ResourceIndex& index) const;

	MaterialInstance& Material()
	{
		return *material;
	}

	std::shared_ptr<MaterialInstance> MaterialPtr() const
	{
		return material;
	}

	ResourceId TextureId(MaterialTextureSlot slot) const;

private:
	std::shared_ptr<MaterialInstance> material;
	Array<ResourceId, 5> textureIds{};
};

Result<std::unique_ptr<EditorDocument>> NewMaterialDocument(const VirtualPath& path, MaterialType type,
                                                            ProjectWorkspace& workspace);

Result<std::unique_ptr<EditorDocument>> OpenMaterialDocument(const VirtualPath& path, ResourceSession& session,
                                                             ProjectWorkspace& workspace);

Result<bool> DrawMaterialInspector(EditorDocument& document, EditorDocuments& documents,
                                   const ProjectWorkspace& workspace, String& diagnostic);
} // namespace Ursa::Editor
