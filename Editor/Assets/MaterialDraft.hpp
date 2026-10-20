#pragma once

#include <Core/Containers.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Serialization/MaterialResource.hpp>

namespace Ursa::Editor
{
class MaterialInspectors;
class InspectorUI;

class MaterialDraft final
{
public:
	explicit MaterialDraft(MaterialType type) : material(std::make_shared<MaterialInstance>(type))
	{
	}

	static Result<std::shared_ptr<MaterialDraft>> Load(const VirtualFileSystem& filesystem, const VirtualPath& path,
	                                                   ResourceSession& session);

	Result<void> SetTextureId(MaterialTextureSlot slot, ResourceId id, ResourceSession& session);

	Result<void> SetPropertyValue(const String& identifier, MaterialPropertyValue value);

	Result<void> SetTextureProperty(const String& identifier, ResourceId id, const Shader::ShaderAsset& shader,
	                                ResourceSession& session);

	Result<void> Save(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id, const ResourceIndex& index,
	                  const ResourceSession::AssetPathResolver& assetResolver) const;

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

Result<void> SetShaderMaterialProperty(EditorDocument& document, const String& identifier, MaterialPropertyValue value);

Result<void> SetShaderMaterialTexture(EditorDocument& document, const String& identifier, ResourceId textureId,
                                      ResourceSession& session);

Result<bool> DrawMaterialInspector(EditorDocument& document, EditorDocuments& documents,
                                   const ProjectWorkspace& workspace, String& diagnostic,
                                   const MaterialInspectors* customInspectors = nullptr,
                                   InspectorUI* inspectorUI = nullptr, bool embedded = false);
} // namespace Ursa::Editor
