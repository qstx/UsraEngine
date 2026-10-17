#include <Scripting/Bindings/ResourceServices.hpp>
#include <Scripting/Bindings/RuntimeServices.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/Bindings/BindingAssets.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Rendering/Shader/ComputeKernel.hpp>

namespace Ursa::Scripting::Bindings
{
Result<ResourceServices::Handle> ResourceServices::LoadTexture(BindingCall& call, const Asset& reference)
{
	auto& api = RuntimeServices::SceneApi(call);
	if (!api.resources)
	{
		return Result<Handle>::Failure("Texture loading requires a ResourceSession");
	}
	const auto asset = ReadAssetValue(reference);
	const auto texture = TakeBindingResult(api.resources->LoadTexture(asset.guid, asset.localFileId));
	const auto handle =
	    TakeBindingResult(api.handles->Register(const_cast<MaterialTexture*>(texture.get()), ManagedHandleKind::Asset));
	api.ownedTextures[handle.index] = texture;
	return Result<Handle>::Success(WriteManagedHandle<Handle>(handle));
}

Result<bool> ResourceServices::IsValid(BindingCall& call, const Handle& resource)
{
	auto& api = RuntimeServices::SceneApi(call);
	const auto handle = ReadManagedHandle(resource);
	bool valid = false;
	switch (handle.kind)
	{
		case ManagedHandleKind::Asset:
			valid = bool(api.handles->Resolve(handle, ManagedHandleKind::Asset)) &&
			        api.ownedTextures.find(handle.index) != api.ownedTextures.end();
			break;
		case ManagedHandleKind::Mesh:
			valid = bool(api.ResolveMesh(handle)) && handle.sessionId != 0;
			break;
		case ManagedHandleKind::Material:
			valid = bool(api.ResolveMaterial(handle)) && handle.sessionId != 0;
			break;
		case ManagedHandleKind::RenderTexture:
			valid = bool(api.ResolveRenderTexture(handle));
			break;
		case ManagedHandleKind::GraphicsBuffer:
			valid = bool(api.ResolveGraphicsBuffer(handle));
			break;
		default:
			break;
	}
	return Result<bool>::Success(valid);
}

Result<ResourceServices::OptionalHandle> ResourceServices::GetMeshBinding(BindingCall& call, const Handle& component,
                                                                          bool instance)
{
	auto& api = RuntimeServices::SceneApi(call);
	auto* target = TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component)));
	MeshPtr mesh;
	if (auto* filter = dynamic_cast<MeshFilterComponent*>(target))
	{
		mesh = instance ? TakeBindingResult(filter->Mesh()) : filter->SharedMesh();
	}
	else if (auto* skin = dynamic_cast<SkinnedMeshRendererComponent*>(target))
	{
		mesh = skin->SharedMesh();
	}
	else
	{
		return Result<OptionalHandle>::Failure("Component has no Mesh binding");
	}
	OptionalHandle result;
	if (mesh)
	{
		result.present = 1;
		result.F_value = WriteManagedHandle<Handle>(TakeBindingResult(api.RegisterMesh(mesh)));
	}
	return Result<OptionalHandle>::Success(result);
}

Result<void> ResourceServices::SetMeshBinding(BindingCall& call, const Handle& component, const OptionalHandle& mesh,
                                              bool instance)
{
	auto& api = RuntimeServices::SceneApi(call);
	auto* target = TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component)));
	MeshPtr value;
	if (mesh.present)
	{
		if (!mesh.F_value.F_sessionId)
		{
			return Result<void>::Failure("A non-null Mesh reference requires a valid handle");
		}
		value = TakeBindingResult(api.ResolveMesh(ReadManagedHandle(mesh.F_value)));
	}
	if (auto* filter = dynamic_cast<MeshFilterComponent*>(target))
	{
		return instance ? filter->SetMesh(std::move(value)) : filter->SetSharedMesh(std::move(value));
	}
	if (auto* skin = dynamic_cast<SkinnedMeshRendererComponent*>(target))
	{
		return skin->SetSharedMesh(std::move(value));
	}
	return Result<void>::Failure("Component has no Mesh binding");
}

Result<DataView> ResourceServices::GetMaterialBindings(BindingCall& call, const Handle& component, bool instance,
                                                       bool first)
{
	auto& api = RuntimeServices::SceneApi(call);
	auto* renderer =
	    dynamic_cast<RendererComponent*>(TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component))));
	if (!renderer)
	{
		return Result<DataView>::Failure("Component is not a Renderer");
	}
	Vector<std::shared_ptr<MaterialInstance>> values;
	if (first)
	{
		values = {instance ? TakeBindingResult(renderer->Material()) : renderer->SharedMaterial()};
	}
	else
	{
		values = instance ? TakeBindingResult(renderer->Materials()) : renderer->SharedMaterials();
	}
	auto* result = static_cast<OptionalHandle*>(call.arena.Allocate(values.size(), sizeof(OptionalHandle)));
	for (Size index = 0; index < values.size(); ++index)
	{
		if (values[index])
		{
			result[index].present = 1;
			result[index].F_value = WriteManagedHandle<Handle>(TakeBindingResult(api.RegisterMaterial(values[index])));
		}
	}
	return Result<DataView>::Success({result, values.size(), 1, 0});
}

Result<void> ResourceServices::SetMaterialBindings(BindingCall& call, const Handle& component,
                                                   const DataView& materials, bool shared, bool first)
{
	auto& api = RuntimeServices::SceneApi(call);
	auto* renderer =
	    dynamic_cast<RendererComponent*>(TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component))));
	if (!renderer || materials.length > Mesh::maxSubMeshes || (first && materials.length != 1))
	{
		return Result<void>::Failure("Renderer slot target or count is invalid");
	}
	const auto* source = static_cast<const OptionalHandle*>(materials.data);
	Vector<std::shared_ptr<MaterialInstance>> values;
	values.reserve(Size(materials.length));
	for (UInt64 index = 0; index < materials.length; ++index)
	{
		if (!source[index].present)
		{
			values.push_back(nullptr);
		}
		else
		{
			if (!source[index].F_value.F_sessionId)
			{
				return Result<void>::Failure("A non-null Material reference requires a valid handle");
			}
			values.push_back(TakeBindingResult(api.ResolveMaterial(ReadManagedHandle(source[index].F_value))));
		}
	}
	if (first)
	{
		return shared ? renderer->SetSharedMaterial(values[0]) : renderer->SetMaterial(values[0]);
	}
	return shared ? renderer->SetSharedMaterials(std::move(values)) : renderer->SetMaterials(std::move(values));
}

Result<Int32> ResourceServices::FindComputeKernel(BindingCall& call, const Asset& reference, const Utf8View& name)
{
	auto& api = RuntimeServices::SceneApi(call);
	const auto asset = ReadAssetValue(reference);
	if (!api.resources || asset.localFileId)
	{
		return Result<Int32>::Failure("ComputeShader requires a ResourceSession and root Shader reference");
	}
	const auto shader = TakeBindingResult(api.resources->LoadShader(asset.guid));
	const auto kernel = TakeBindingResult(Rendering::FindComputeKernel(*shader, ReadString(name)));
	if (kernel > UInt32(std::numeric_limits<Int32>::max()))
	{
		return Result<Int32>::Failure("Compute kernel index exceeds managed capacity");
	}
	return Result<Int32>::Success(Int32(kernel));
}

Result<Wire::Value_Ursa_ComputeThreadGroupSize>
ResourceServices::GetComputeThreadGroupSize(BindingCall& call, const Asset& reference, Int32 kernel)
{
	auto& api = RuntimeServices::SceneApi(call);
	const auto asset = ReadAssetValue(reference);
	if (!api.resources || asset.localFileId || kernel < 0)
	{
		return Result<Wire::Value_Ursa_ComputeThreadGroupSize>::Failure("ComputeShader reference or kernel is invalid");
	}
	const auto shader = TakeBindingResult(api.resources->LoadShader(asset.guid));
	const auto* selected = TakeBindingResult(Rendering::ComputeKernelAt(*shader, UInt32(kernel)));
	return Result<Wire::Value_Ursa_ComputeThreadGroupSize>::Success(
	    {selected->threadGroupSize[0], selected->threadGroupSize[1], selected->threadGroupSize[2]});
}
} // namespace Ursa::Scripting::Bindings
