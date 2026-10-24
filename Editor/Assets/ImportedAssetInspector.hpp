#pragma once

#include <Scripting/ManagedEditorBridge.hpp>

namespace Ursa::Editor
{
// A selected published object is a read-only resource target, independent of its source importer draft.
class ImportedAssetInspector final : public std::enable_shared_from_this<ImportedAssetInspector>
{
public:
	static Result<std::shared_ptr<ImportedAssetInspector>> Open(ProjectWorkspace& workspace, AssetReference reference,
	                                                            const String& kind);
	ManagedEditorContext Context();
	Result<void> Draw(const std::function<Result<bool>(ManagedEditorContext&)>& custom);

private:
	ProjectWorkspace* workspace = nullptr;
	AssetReference reference;
	String identity;
	std::unique_ptr<ResourceSession> resources;
	std::unique_ptr<ComponentLoadContext> loadContext;
	MeshPtr mesh;
	MaterialTexturePtr texture;
	std::shared_ptr<MaterialInstance> material;
	GameObject::Ptr root;
	std::shared_ptr<const ModelSkeleton> skeleton;
	std::shared_ptr<const AnimationClip> clip;
	nlohmann::json payload;
};
} // namespace Ursa::Editor
