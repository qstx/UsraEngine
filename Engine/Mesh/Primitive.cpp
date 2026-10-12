#include <Mesh/Primitive.hpp>
#include <Scene/Scene.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Serialization/ResourceSession.hpp>

namespace Ursa
{
ResourceId BuiltinCubeMeshId()
{
	return ResourceId::Parse("20000000-0000-4000-8000-000000000001").value;
}

ResourceId BuiltinDefaultMaterialId()
{
	return ResourceId::Parse("20000000-0000-4000-8000-000000000002").value;
}

Result<GameObject::Ptr> CreatePrimitive(Scene& scene, ResourceSession& resources, PrimitiveType type, String name)
{
	if (type != PrimitiveType::Cube)
	{
		return Result<GameObject::Ptr>::Failure("Unsupported primitive type");
	}
	auto mesh = resources.LoadMesh(BuiltinCubeMeshId(), 0);
	auto material = resources.LoadMaterial(BuiltinDefaultMaterialId());
	if (!mesh || !material)
	{
		return Result<GameObject::Ptr>::Failure(mesh ? material.error : mesh.error);
	}
	auto object = GameObject::Create(std::move(name));
	auto filter = object->AddComponent<MeshFilterComponent>();
	auto renderer = object->AddComponent<MeshRendererComponent>();
	if (!filter || !renderer)
	{
		return Result<GameObject::Ptr>::Failure(filter ? renderer.error : filter.error);
	}
	auto result = filter.value->SetSharedMesh(std::move(mesh.value));
	if (result)
	{
		result = renderer.value->SetSharedMaterial(std::move(material.value));
	}
	if (result)
	{
		result = scene.AttachGameObject(object);
	}
	return result ? Result<GameObject::Ptr>::Success(std::move(object))
	              : Result<GameObject::Ptr>::Failure(result.error);
}
} // namespace Ursa
