#include <Scripting/ManagedSceneApi.hpp>

namespace Ursa::Scripting
{
Result<ManagedHandle> ManagedSceneApi::RegisterMesh(const MeshPtr& mesh)
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
		ownedMeshes[handle.value.index] = mesh;
	}
	return handle;
}

Result<ManagedHandle> ManagedSceneApi::RegisterMaterial(const std::shared_ptr<MaterialInstance>& material)
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
		ownedMaterials[handle.value.index] = material;
	}
	return handle;
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
