#pragma once

#include <Assets/AssetDatabase.hpp>
#include <Material/Material.hpp>
#include <Mesh/Mesh.hpp>
#include <Scene/LocalTransform.hpp>

namespace Ursa::Editor
{
struct ModelCandidateNode
{
	UInt64 localFileId = 0;
	UInt64 parentLocalFileId = 0;
	String name;
	LocalTransform transform;
	Vector<UInt64> meshes;
	Vector<UInt64> children;
};

struct ModelCandidateMaterial
{
	UInt64 localFileId = 0;
	String name;
	std::shared_ptr<MaterialInstance> material;
	Map<MaterialTextureSlot, AssetReference> textures;
};

struct ModelCandidateArchive
{
	String kind;
	nlohmann::json document;
};

// Owned CPU candidates. No primary model artifact is encoded or published by construction.
struct ModelImportCandidate
{
	ResourceId guid;
	UInt64 mainObjectLocalFileId = 0;
	UInt64 rootNodeLocalFileId = 0;
	Vector<ModelCandidateNode> nodes;
	Vector<UInt64> meshOrder;
	Vector<UInt64> animationOrder;
	Map<UInt64, MeshPtr> meshes;
	Map<UInt64, ModelCandidateMaterial> materials;
	Map<UInt64, MaterialTexturePtr> textures;
	Map<UInt64, Vector<UInt64>> meshMaterialBindings;
	Map<UInt64, ModelCandidateArchive> archives;
	Map<UInt64, std::shared_ptr<const ModelSkeleton>> skeletons;
	Map<UInt64, String> outputKeys;
	Vector<VirtualPath> dependencies;
	Vector<String> diagnostics;

	Result<void> Validate() const;
	Result<Vector<ImportedArtifact>> Encode() const;
};
} // namespace Ursa::Editor
