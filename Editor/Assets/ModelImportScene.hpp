#pragma once

#include <Assets/ModelImportCandidate.hpp>
#include <Scene/Scene.hpp>

namespace Ursa::Editor
{
// A private editable view of the unencoded candidate. Source node identities remain stable through edits.
class ModelImportScene final
{
public:
	ModelImportScene(ModelImportCandidate& candidate, Scene& scene);
	Result<GameObject::Ptr> Build();
	Result<void> Apply();

private:
	UInt64 OutputId(const String& key);
	UInt64 MaterialId(const std::shared_ptr<MaterialInstance>& material, const String& key);
	UInt64 MeshId(const MeshPtr& mesh, const String& key);
	ModelImportCandidate& candidate;
	Scene& scene;
	GameObject::Ptr root;
	Vector<GameObject::Ptr> originalObjects;
	Map<const GameObject*, UInt64> nodeIds;
	Map<const Mesh*, UInt64> meshIds;
	Map<const MaterialInstance*, UInt64> materialIds;
};
} // namespace Ursa::Editor
