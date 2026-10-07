#pragma once
#include <Vfs/VirtualFileSystem.hpp>

#include <Scene/GameObject.hpp>
#include <Serialization/ResourceId.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

namespace Ursa
{
class ComponentRegistry;
class ComponentLoadContext;
class Scene;
class MaterialInstance;

class PrefabAsset final : public Serializable
{
public:
	String SerializationType() const override
	{
		return "ursa.PrefabAsset";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	PrefabAsset() = default;

	PrefabAsset(ResourceId assetId, GameObject::Ptr rootObject) : id(assetId), root(std::move(rootObject))
	{
	}

	ResourceId Id() const
	{
		return id;
	}

	const GameObject::Ptr& Root() const
	{
		return root;
	}

	Result<GameObject::Ptr> Instantiate(
	    Scene& scene, const ComponentRegistry& registry, ComponentLoadContext& context,
	    const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId = {}) const;

private:
	ResourceId id;
	GameObject::Ptr root;
};

Result<PrefabAsset>
CapturePrefabAsset(ResourceId id, const Scene& scene, GameObject::Ptr root, const ComponentRegistry& registry,
                   ComponentLoadContext& context,
                   const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId = {});

Result<nlohmann::json> EncodePrefabAssetDocument(
    const String& label, const PrefabAsset& asset, const ComponentRegistry& registry,
    const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId = {});

Result<PrefabAsset> DecodePrefabAssetDocument(const String& label, const nlohmann::json& document,
                                              const ComponentRegistry& registry, ComponentLoadContext& context);
Result<void> SavePrefabAssetResource(
    VirtualFileSystem& filesystem, const VirtualPath& path, const PrefabAsset& asset, const ComponentRegistry& registry,
    const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId = {});

Result<PrefabAsset> LoadPrefabAssetResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                            const ComponentRegistry& registry, ComponentLoadContext& context);

} // namespace Ursa
