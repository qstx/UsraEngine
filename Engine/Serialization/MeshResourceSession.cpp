#include <Serialization/ResourceSession.hpp>
#include <Serialization/MeshResource.hpp>
#include <cstring>
#include <Core/Memory/MemoryFactories.hpp>

namespace Ursa
{
Result<void> ResourceSession::RememberMeshAsset(const MeshPtr& mesh)
{
	if (!mesh || !mesh->IsValid() || !mesh->AssetIdentity().guid)
	{
		return Result<void>::Failure("ResourceSession can only cache valid asset Mesh objects");
	}
	const auto reference = mesh->AssetIdentity();
	const String key = reference.guid.ToString() + ":" + String(std::to_string(reference.localFileId).c_str());
	geometry[key] = mesh;
	return {};
}

Result<std::unique_ptr<ResourceSession>> ResourceSession::Isolated() const
{
	auto session = Open(index.Filesystem(), assetPathResolver);
	if (!session)
	{
		return session;
	}
	session.value->textures = textures;
	session.value->shaders = shaders;
	session.value->skeletons = skeletons;
	session.value->animationClips = animationClips;
	for (const auto& entry : geometry)
	{
		auto mesh = entry.second->Clone();
		if (!mesh)
		{
			return Result<std::unique_ptr<ResourceSession>>::Failure(mesh.error);
		}
		auto bound = mesh.value->BindAssetIdentity(entry.second->AssetIdentity());
		if (!bound)
		{
			return Result<std::unique_ptr<ResourceSession>>::Failure(bound.error);
		}
		session.value->geometry[entry.first] = std::move(mesh.value);
	}
	for (const auto& entry : materials)
	{
		auto material = entry.second->Clone();
		if (!material)
		{
			return Result<std::unique_ptr<ResourceSession>>::Failure("Cannot isolate a destroyed material");
		}
		auto bound = material->BindAssetIdentity(entry.second->AssetIdentity());
		if (!bound)
		{
			return Result<std::unique_ptr<ResourceSession>>::Failure(bound.error);
		}
		session.value->materials[entry.first] = std::move(material);
	}
	return session;
}

Result<MeshPtr> ResourceSession::LoadMesh(ResourceId guid, UInt64 localFileId)
{
	const String key = guid.ToString() + ":" + String(std::to_string(localFileId).c_str());
	auto cached = geometry.find(key);
	if (cached != geometry.end() && cached->second->IsValid())
	{
		return Result<MeshPtr>::Success(cached->second);
	}
	auto path = ResolveAssetPath(guid, localFileId);
	if (!path)
	{
		return Result<MeshPtr>::Failure(path.error);
	}
	auto kind = ResourceKindForPath(path.value);
	if (kind && kind.value == ResourceKind::Mesh)
	{
		SerializationContext context(this);
		auto loaded = LoadMeshResource(index.Filesystem(), path.value, context);
		if (!loaded)
		{
			return Result<MeshPtr>::Failure(loaded.error);
		}
		if (localFileId != loaded.value.localFileId || loaded.value.id != guid)
		{
			return Result<MeshPtr>::Failure("Independent Mesh GUID or local-file-ID mismatch");
		}
		geometry[key] = loaded.value.mesh;
		return Result<MeshPtr>::Success(loaded.value.mesh);
	}
	return Result<MeshPtr>::Failure("Mesh resource has an unsupported format at " + path.value.ToString());
}
} // namespace Ursa
