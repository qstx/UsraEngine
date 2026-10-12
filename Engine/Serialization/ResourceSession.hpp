#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Material/Material.hpp>
#include <Animation/AnimationAssets.hpp>
#include <Mesh/Mesh.hpp>
#include <Serialization/AssetReference.hpp>
#include <Serialization/ResourceIndex.hpp>
#include <Shader/ShaderAsset.hpp>
#include <functional>
#include <memory>

namespace Ursa
{
class ResourceReload;

// Loads and caches resources by ID through the index's virtual file system.
class ResourceSession final
{
public:
	using AssetPathResolver = std::function<Result<VirtualPath>(ResourceId, Ursa::UInt64)>;

	static Result<std::unique_ptr<ResourceSession>> Open(const Vector<ResourceMount>& mounts,
	                                                     AssetPathResolver assetPathResolver = {});

	static Result<std::unique_ptr<ResourceSession>> Open(const VirtualFileSystem& filesystem,
	                                                     AssetPathResolver assetPathResolver = {});

	// Convenience for a caller that owns a single directory of resources; the
	// directory is mounted under the conventional project scheme.
	static Result<std::unique_ptr<ResourceSession>> Open(const std::filesystem::path& root,
	                                                     MountAccess access = MountAccess::ReadWrite);

	Result<std::unique_ptr<ResourceSession>> Refreshed() const;

	Result<std::unique_ptr<ResourceSession>> Isolated() const;

	Result<void> ReloadImportedResources(const VirtualFileSystem& files,
	                                     const MaterialInstance* protectedMaterial = nullptr);

	Result<std::shared_ptr<ResourceReload>> PrepareResourceReload(const VirtualFileSystem& files,
	                                                              const MaterialInstance* protectedMaterial = nullptr);

	void CommitResourceReload(std::shared_ptr<ResourceReload> reload);

	Result<MaterialTexturePtr> LoadTexture(ResourceId id);

	Result<MaterialTexturePtr> LoadTexture(ResourceId guid, Ursa::UInt64 localFileId);

	Result<std::shared_ptr<MaterialInstance>> LoadMaterial(ResourceId id);

	Result<std::shared_ptr<MaterialInstance>> LoadMaterial(ResourceId guid, Ursa::UInt64 localFileId);

	Result<MeshPtr> LoadMesh(ResourceId guid, UInt64 localFileId);

	Result<void> RememberMeshAsset(const MeshPtr& mesh);

	Result<std::shared_ptr<const ModelSkeleton>> LoadSkeleton(ResourceId modelGuid, Ursa::UInt64 localFileId);

	Result<std::shared_ptr<const AnimationClip>> LoadAnimationClip(ResourceId modelGuid, Ursa::UInt64 localFileId);

	Result<VirtualPath> ResolveAsset(ResourceId guid, Ursa::UInt64 localFileId = 0) const
	{
		return ResolveAssetPath(guid, localFileId);
	}

	Result<std::shared_ptr<const Shader::ShaderAsset>> LoadShader(ResourceId id);

	Result<ResourceId> IdentifyMaterial(const std::shared_ptr<MaterialInstance>& material) const;

	Result<AssetReference> IdentifyMaterialReference(const std::shared_ptr<MaterialInstance>& material) const;

	const ResourceIndex& Index() const
	{
		return index;
	}

	ResourceIndex& Index()
	{
		return index;
	}

private:
	Result<VirtualPath> ResolveAssetPath(ResourceId guid, Ursa::UInt64 localFileId) const;

	Result<ResourceLocation> ResolveResource(ResourceId id) const;

	Result<std::shared_ptr<const Shader::ShaderAsset>> LoadShaderInternal(ResourceId id, Set<String>& active);

	ResourceIndex index;
	AssetPathResolver assetPathResolver;
	Map<String, MaterialTexturePtr> textures;
	Map<String, std::shared_ptr<MaterialInstance>> materials;
	Map<String, std::shared_ptr<const Shader::ShaderAsset>> shaders;
	Map<String, MeshPtr> geometry;
	Map<String, std::shared_ptr<const ModelSkeleton>> skeletons;
	Map<String, std::shared_ptr<const AnimationClip>> animationClips;
};
} // namespace Ursa
