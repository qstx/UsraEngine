#include <Scene/MeshFilterComponent.hpp>
#include <Scene/RendererComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Scene/SkinnedMeshRendererComponent.hpp>
#include <Scene/Scene.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

class TestRenderer final : public RendererComponent
{
public:
	String SerializationType() const override
	{
		return "test.Renderer";
	}
};

class OtherRenderer final : public RendererComponent
{
public:
	String SerializationType() const override
	{
		return "test.OtherRenderer";
	}
};

class ReplaceFilter final : public Component
{
public:
	explicit ReplaceFilter(MeshPtr mesh) : mesh(std::move(mesh))
	{
	}

	Result<void> Update(const FrameContext&) override
	{
		if (replaced)
		{
			return {};
		}
		auto owner = GetGameObject();
		auto* old = owner->FindComponent<MeshFilterComponent>();
		auto removed = owner->RemoveComponent(old);
		if (!removed)
		{
			return removed;
		}
		auto created = owner->AddComponent<MeshFilterComponent>();
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		replaced = true;
		return created.value->SetSharedMesh(mesh);
	}

private:
	MeshPtr mesh;
	bool replaced = false;
};

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

void GeometryBinding()
{
	auto a = GameObject::Create("A");
	auto b = GameObject::Create("B");
	auto* filterA = Take(a->AddComponent<MeshFilterComponent>());
	auto* filterB = Take(b->AddComponent<MeshFilterComponent>());
	MeshData data;
	data.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	data.indices = {0, 1, 2};
	auto shared = Take(Mesh::Create(data));
	Done(filterA->SetSharedMesh(shared));
	Done(filterB->SetSharedMesh(shared));
	auto instance = Take(filterA->Mesh());
	Expect(instance != shared && Take(filterA->Mesh()) == instance && filterA->SharedMesh() == instance,
	       "Mesh getter did not bind and reuse one independent instance");
	Done(instance->SetVertices({{2, 0, 0}, {1, 0, 0}, {0, 1, 0}}));
	Expect(filterB->SharedMesh()->Snapshot()->data.positions[0].x == 0, "Mesh instance changed shared geometry");
	Expect(!a->AddComponent<MeshFilterComponent>(), "Duplicate Filter was accepted");
	Expect(!filterA->SetEnabled(false) && filterA->IsEnabled(), "Data component accepted enabled mutation");
	Done(filterA->SetSharedMesh(nullptr));
	ArchiveWriter empty;
	Done(filterA->Serialize(empty));
	SerializationContext context;
	MeshFilterComponent detached;
	Done(detached.Deserialize(Take(JsonArchive::Decode(Take(JsonArchive::Encode(empty)))), context));
	auto newMesh = Take(detached.Mesh());
	Expect(newMesh->Snapshot()->data.positions.empty(), "Empty Filter did not create an empty Mesh");
	PrefabAsset prefab(ResourceId::Generate(), b);
	Expect(!filterB->Mesh() && filterB->SharedMesh() == shared, "Prefab source implicitly cloned Mesh");
	auto child = GameObject::Create("Child");
	Done(child->SetParent(b));
	auto* childFilter = Take(child->AddComponent<MeshFilterComponent>());
	Expect(child->IsAssetTemplate() && !childFilter->Mesh(), "New Prefab child lost persistent ownership");
}

void SceneCollection()
{
	Scene scene;
	auto owner = Take(scene.CreateGameObject("Mesh"));
	auto* renderer = Take(owner->AddComponent<MeshRendererComponent>());
	Camera camera;
	Expect(Take(scene.CollectPreviewRenderData(camera, 1)).boxes.empty(), "Renderer without Filter drew a cube");
	auto* filter = Take(owner->AddComponent<MeshFilterComponent>());
	Expect(Take(scene.CollectPreviewRenderData(camera, 1)).boxes.empty(), "Empty Filter drew a cube");
	MeshData data;
	data.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	data.indices = {0, 1, 2, 0, 2, 1};
	data.subMeshes = {{0, 3, 0}, {3, 3, 0}};
	auto mesh = Take(Mesh::Create(std::move(data)));
	Done(filter->SetSharedMesh(mesh));
	auto red = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	auto green = red->Clone();
	Done(red->SetBaseColor({1, 0, 0, 1}));
	Done(green->SetBaseColor({0, 1, 0, 1}));
	Done(renderer->SetSharedMaterials({red, nullptr, green}));
	auto draws = Take(scene.CollectPreviewRenderData(camera, 1)).boxes;
	Expect(draws.size() == 3 && draws[0].subMesh == 0 && draws[1].subMesh == 1 && draws[2].subMesh == 1 &&
	           draws[0].material == red && !draws[1].material && draws[2].material == green,
	       "Submesh or missing/extra material ordering was incorrect");
	Done(renderer->SetEnabled(false));
	Expect(Take(scene.CollectPreviewRenderData(camera, 1)).boxes.empty(), "Disabled Renderer still drew");
	Done(renderer->SetEnabled(true));
	Take(owner->AddComponent<ReplaceFilter>(mesh));
	scene.BeginFrame();
	Done(scene.Update({}));
	Expect(Take(scene.CollectPreviewRenderData(camera, 1)).boxes.size() == 3,
	       "Filter replacement during Update found the removed component instead of its replacement");
	filter = owner->FindComponent<MeshFilterComponent>();
	Done(owner->RemoveComponent(filter));
	Expect(Take(scene.CollectPreviewRenderData(camera, 1)).boxes.empty(),
	       "Removed Filter remained in scene collection");
	Expect(!owner->AddComponent<SkinnedMeshRendererComponent>(), "Static and skinned Renderers coexisted");
	Done(owner->RemoveComponent(renderer));
	auto* skin = Take(owner->AddComponent<SkinnedMeshRendererComponent>());
	Expect(!skin->SetSharedMesh(mesh), "Skinned Renderer accepted geometry without skin binding");
	Done(skin->SetSharedMesh(nullptr));
}

void MaterialBinding()
{
	auto a = GameObject::Create();
	auto b = GameObject::Create();
	auto* rendererA = Take(a->AddComponent<TestRenderer>());
	auto* rendererB = Take(b->AddComponent<TestRenderer>());
	auto shared = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(shared->SetBaseColor({1, 0, 0, 1}));
	Done(rendererA->SetSharedMaterials({shared, shared, nullptr}));
	Done(rendererB->SetSharedMaterial(shared));
	auto first = Take(rendererA->Material());
	Expect(first != shared && Take(rendererA->Material()) == first && rendererA->SharedMaterial() == first,
	       "Material getter did not instantiate just the first slot");
	Expect(rendererA->SharedMaterials()[1] == shared, "First Material getter cloned another slot");
	Done(first->SetBaseColor({0, 1, 0, 1}));
	Expect(rendererB->SharedMaterial()->Parameters().baseColor.red == 1,
	       "Independent material changed another Renderer");
	auto all = Take(rendererA->Materials());
	Expect(all[0] == first && all[1] != shared && !all[2],
	       "Materials getter did not retain existing instance or null slot");
	Expect(Take(rendererA->Materials())[1] == all[1], "Repeated getter cloned a slot again");
	all.clear();
	Expect(rendererA->SharedMaterials().size() == 3, "Array getter exposed the binding array");
	Expect(!a->AddComponent<OtherRenderer>(), "A second Renderer subtype was accepted");
	Done(first->Destroy());
	Expect(!rendererA->Material() && !rendererB->SetSharedMaterial(first) && !first->SetBaseColor({}),
	       "Destroyed material remained mutable or bindable");
	PrefabAsset prefab(ResourceId::Generate(), b);
	Expect(!rendererB->Material(), "Prefab source implicitly cloned a material");
	Scene scene;
	auto owner = Take(scene.CreateGameObject());
	Take(scene.AddComponent<TestRenderer>(owner));
	Expect(!scene.AddComponent<OtherRenderer>(owner), "Scene attachment bypassed Renderer uniqueness");
}
} // namespace

int main()
{
	try
	{
		GeometryBinding();
		MaterialBinding();
		SceneCollection();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
