#pragma once

#include <Mesh/Primitive.hpp>
#include <Scene/Scene.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Serialization/MeshResource.hpp>
#include <Core/Memory/MemoryFactories.hpp>

namespace Ursa::Test
{
inline Result<MeshPtr> Cube()
{
	static MeshPtr geometry;
	if (!geometry)
	{
		VirtualFileSystem files;
		const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Assets";
		auto mounted = files.Mount("ursa", root, MountAccess::ReadOnly);
		if (!mounted)
		{
			return Result<MeshPtr>::Failure(mounted.error);
		}
		SerializationContext context;
		auto loaded = LoadMeshResource(files, VirtualPath::Parse("ursa:Meshes/Cube.ursamesh").value, context);
		if (!loaded)
		{
			return Result<MeshPtr>::Failure(loaded.error);
		}
		auto copied = loaded.value.mesh->Clone();
		if (!copied)
		{
			return copied;
		}
		geometry = std::move(copied.value);
	}
	return Result<MeshPtr>::Success(geometry);
}

inline Result<MeshRendererComponent*> AddRenderer(const GameObject::Ptr& object, Color color = {})
{
	auto geometry = Cube();
	if (!geometry)
	{
		return Result<MeshRendererComponent*>::Failure(geometry.error);
	}
	auto* filter = object->FindComponent<MeshFilterComponent>();
	if (!filter)
	{
		auto created = object->AddComponent<MeshFilterComponent>();
		if (!created)
		{
			return Result<MeshRendererComponent*>::Failure(created.error);
		}
		filter = created.value;
	}
	auto renderer = object->AddComponent<MeshRendererComponent>();
	if (!renderer)
	{
		return renderer;
	}
	auto assigned = filter->SetSharedMesh(geometry.value);
	if (assigned)
	{
		auto material = MakeShared<MaterialInstance>(Memory::MemoryTag::Assets, MaterialType::Unlit);
		assigned = material->SetBaseColor(color);
		if (assigned)
		{
			assigned = renderer.value->SetMaterial(std::move(material));
		}
	}
	return assigned ? renderer : Result<MeshRendererComponent*>::Failure(assigned.error);
}

inline Result<ComponentId> AddRenderer(Scene&, const GameObject::Ptr& object, Color color = {})
{
	auto component = AddRenderer(object, color);
	return component ? Result<ComponentId>::Success(component.value->Id())
	                 : Result<ComponentId>::Failure(component.error);
}
} // namespace Ursa::Test
