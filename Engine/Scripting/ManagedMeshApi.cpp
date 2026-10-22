#include <Scripting/ManagedSceneApi.hpp>

namespace Ursa::Scripting
{
Result<std::shared_ptr<void>> ManagedSceneApi::CandidateResource(const ManagedHandle& handle) const
{
	if (readOnlyResources.find(handle.index) != readOnlyResources.end())
	{
		return Result<std::shared_ptr<void>>::Failure(
		    "Persistent read-only assets are references; Clone before registering an output candidate");
	}
	if (!handle.sessionId)
	{
		return Result<std::shared_ptr<void>>::Failure("Candidate resource cannot be null");
	}
	if (handle.kind == ManagedHandleKind::GameObject)
	{
		auto value = Resolve(handle);
		return value ? Result<std::shared_ptr<void>>::Success(value.value)
		             : Result<std::shared_ptr<void>>::Failure(value.error);
	}
	if (handle.kind == ManagedHandleKind::Component)
	{
		auto value = ResolveComponent(handle);
		auto owner = componentOwners.find(handle.index);
		if (!value || owner == componentOwners.end() || owner->second.expired())
		{
			return Result<std::shared_ptr<void>>::Failure("Component candidate has been detached or destroyed");
		}
		return Result<std::shared_ptr<void>>::Success(std::shared_ptr<void>(owner->second.lock(), value.value));
	}
	if (handle.kind == ManagedHandleKind::Mesh)
	{
		auto value = ResolveMesh(handle);
		return value ? Result<std::shared_ptr<void>>::Success(value.value)
		             : Result<std::shared_ptr<void>>::Failure(value.error);
	}
	if (handle.kind == ManagedHandleKind::Material)
	{
		auto value = ResolveMaterial(handle);
		return value ? Result<std::shared_ptr<void>>::Success(value.value)
		             : Result<std::shared_ptr<void>>::Failure(value.error);
	}
	return Result<std::shared_ptr<void>>::Failure("Object is not an owned CPU resource candidate");
}

Result<ManagedHandle> ManagedSceneApi::RegisterMesh(const MeshPtr& mesh, bool writable)
{
	if (!mesh)
	{
		return Result<ManagedHandle>::Success({});
	}
	if (!mesh->IsValid())
	{
		return Result<ManagedHandle>::Failure("Mesh has been destroyed");
	}
	auto handle = handles->Register(mesh.get(), ManagedHandleKind::Mesh);
	if (handle)
	{
		if (ownedMeshes.find(handle.value.index) == ownedMeshes.end() && !writable &&
		    (LocalResourceReferencesReadOnly() || (PersistentResourcesReadOnly() && mesh->AssetIdentity().guid)))
		{
			readOnlyResources.insert(handle.value.index);
		}
		ownedMeshes[handle.value.index] = mesh;
	}
	return handle;
}

Result<ManagedHandle> ManagedSceneApi::RegisterMaterial(const std::shared_ptr<MaterialInstance>& material,
                                                        bool writable)
{
	if (!material)
	{
		return Result<ManagedHandle>::Success({});
	}
	if (!material->IsValid())
	{
		return Result<ManagedHandle>::Failure("Material has been destroyed");
	}
	auto handle = handles->Register(material.get(), ManagedHandleKind::Material);
	if (handle)
	{
		if (ownedMaterials.find(handle.value.index) == ownedMaterials.end() && !writable &&
		    (LocalResourceReferencesReadOnly() || (PersistentResourcesReadOnly() && material->AssetIdentity().guid)))
		{
			readOnlyResources.insert(handle.value.index);
		}
		ownedMaterials[handle.value.index] = material;
	}
	return handle;
}

void ManagedSceneApi::RequireWritableResource(const ManagedHandle& handle) const
{
	RequireResourceAccess();
	const auto value = handles->Resolve(handle, handle.kind);
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	if (readOnlyResources.find(handle.index) != readOnlyResources.end())
	{
		throw std::runtime_error("Persistent assets are read-only in this scope; Clone before editing");
	}
}

Result<MeshPtr> ManagedSceneApi::ResolveMesh(const ManagedHandle& handle) const
{
	if (!handle.sessionId)
	{
		return Result<MeshPtr>::Success(nullptr);
	}
	auto valid = handles->Resolve(handle, ManagedHandleKind::Mesh);
	auto found = ownedMeshes.find(handle.index);
	return valid && found != ownedMeshes.end() && found->second->IsValid()
	           ? Result<MeshPtr>::Success(found->second)
	           : Result<MeshPtr>::Failure(valid ? "Mesh has been destroyed" : valid.error);
}

Result<std::shared_ptr<MaterialInstance>> ManagedSceneApi::ResolveMaterial(const ManagedHandle& handle) const
{
	if (!handle.sessionId)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Success(nullptr);
	}
	auto valid = handles->Resolve(handle, ManagedHandleKind::Material);
	auto found = ownedMaterials.find(handle.index);
	return valid && found != ownedMaterials.end() && found->second->IsValid()
	           ? Result<std::shared_ptr<MaterialInstance>>::Success(found->second)
	           : Result<std::shared_ptr<MaterialInstance>>::Failure(valid ? "Material has been destroyed"
	                                                                      : valid.error);
}

Result<std::shared_ptr<ManagedSceneApi::ManagedRenderTexture>>
ManagedSceneApi::ResolveRenderTexture(const ManagedHandle& handle) const
{
	auto valid = handles->Resolve(handle, ManagedHandleKind::RenderTexture);
	auto found = ownedRenderTextures.find(handle.index);
	return valid && found != ownedRenderTextures.end()
	           ? Result<std::shared_ptr<ManagedRenderTexture>>::Success(found->second)
	           : Result<std::shared_ptr<ManagedRenderTexture>>::Failure("RenderTexture handle is stale");
}

Result<std::shared_ptr<ManagedSceneApi::ManagedGraphicsBuffer>>
ManagedSceneApi::ResolveGraphicsBuffer(const ManagedHandle& handle) const
{
	auto valid = handles->Resolve(handle, ManagedHandleKind::GraphicsBuffer);
	auto found = ownedGraphicsBuffers.find(handle.index);
	return valid && found != ownedGraphicsBuffers.end()
	           ? Result<std::shared_ptr<ManagedGraphicsBuffer>>::Success(found->second)
	           : Result<std::shared_ptr<ManagedGraphicsBuffer>>::Failure("GraphicsBuffer handle is stale");
}

} // namespace Ursa::Scripting
