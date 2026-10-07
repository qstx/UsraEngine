#include <ResourceFixtures.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Containers.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
template <class T> T Take(Ursa::Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
	return std::move(result.value);
}

void done(Ursa::Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void RoundTripScene(const std::filesystem::path& path, Ursa::ResourceSession& resources)
{
	Ursa::ComponentRegistry registry;
	if (!Ursa::RegisterBuiltinComponents(registry))
	{
		return;
	}
	Ursa::Scene source;
	auto node = source.CreateGameObject();
	if (!node || !source.AddComponent<Ursa::CameraComponent>(node.value))
	{
		return;
	}
	if (!Ursa::SaveSceneResource(path, Ursa::ResourceId::Generate(), source, registry))
	{
		return;
	}
	if (!Ursa::WriteResourceMetadata(path))
	{
		return;
	}
	Ursa::ComponentLoadContext context(resources);
	auto loaded = Ursa::LoadSceneResource(path, registry, context);
	if (!loaded)
	{
		return;
	}
	std::unique_ptr<Ursa::Scene> active = std::move(loaded.value.scene);
	auto reloaded = Ursa::ReloadSceneResource(path, registry, context, active);
	if (!reloaded)
	{
		throw std::runtime_error(Ursa::ToStdString(reloaded.error));
	}
}

void TestScene(const std::filesystem::path& root)
{
	Ursa::ComponentRegistry registry;
	done(Ursa::RegisterBuiltinComponents(registry));
	Ursa::Scene scene;
	const auto first = Take(scene.CreateGameObject());
	const auto child = Take(scene.CreateGameObject(first));
	const auto second = Take(scene.CreateGameObject());
	done(scene.ReorderGameObject(second, 0));
	Ursa::LocalTransform transform;
	transform.position = {2.0f, 3.0f, 4.0f};
	transform.scale = {1.2f, 0.8f, 2.0f};
	done(scene.SetLocalTransform(child, transform));
	done(scene.SetEnabled(child, false));
	done(scene.SetAmbientLight({0.3f, 0.4f, 0.5f, 1.0f}, 0.25f));
	const auto camera = Take(scene.AddComponent<Ursa::CameraComponent>(first));
	auto* cameraObject = static_cast<Ursa::CameraComponent*>(scene.GetComponent(camera));
	cameraObject->verticalFovRadians = 0.9f;
	cameraObject->nearPlane = 0.2f;
	cameraObject->farPlane = 80.0f;
	done(scene.SetActiveCamera(camera));
	const auto box = Take(scene.AddComponent<Ursa::BoxRenderComponent>(child, Ursa::Color{0.9f, 0.1f, 0.2f, 1.0f}));
	done(scene.SetComponentEnabled(box, false));
	const auto sun = Take(scene.AddComponent<Ursa::DirectionalLightComponent>(second));
	auto* sunObject = static_cast<Ursa::DirectionalLightComponent*>(scene.GetComponent(sun));
	done(sunObject->SetColor({0.8f, 0.7f, 0.6f, 1.0f}));
	done(sunObject->SetIntensity(2.0f));
	const auto lamp = Take(scene.AddComponent<Ursa::PointLightComponent>(second));
	auto* lampObject = static_cast<Ursa::PointLightComponent*>(scene.GetComponent(lamp));
	done(lampObject->SetRange(7.0f));
	Take(scene.AddComponent<Ursa::PointLightComponent>(second));
	auto material = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::Unlit);
	done(material->SetBaseColor({0.2f, 0.5f, 0.8f, 1.0f}));
	const auto materialId = Ursa::ResourceId::Generate();
	const auto materialPath = root / "box.ursamat";
	done(Ursa::SaveMaterialResource(materialPath, materialId, *material, {}));
	done(Ursa::WriteResourceMetadata(materialPath));
	const auto visibleBox = Take(scene.AddComponent<Ursa::BoxRenderComponent>(first));
	static_cast<Ursa::BoxRenderComponent*>(scene.GetComponent(visibleBox))->material = material;
	const auto firstId = Take(scene.PersistentId(first));
	const auto childId = Take(scene.PersistentId(child));
	const auto secondId = Take(scene.PersistentId(second));
	const auto cameraId = Take(scene.ComponentPersistentId(camera));
	const auto sunId = Take(scene.ComponentPersistentId(sun));
	const auto sceneId = Ursa::ResourceId::Generate();
	const auto path = root / "sample.ursascene";
	done(Ursa::SaveSceneResource(path, sceneId, scene, registry,
	                             [material, materialId](const std::shared_ptr<Ursa::MaterialInstance>& selected)
	                             {
		                             return selected == material
		                                        ? Ursa::Result<Ursa::ResourceId>::Success(materialId)
		                                        : Ursa::Result<Ursa::ResourceId>::Failure("Unknown material");
	                             }));
	done(Ursa::WriteResourceMetadata(path));
	auto session = Take(Ursa::ResourceSession::Open({root}));
	Ursa::ComponentLoadContext context(*session);
	auto loaded = Take(Ursa::LoadSceneResource(path, registry, context));
	Expect(loaded.id == sceneId && loaded.scene->RootGameObjects().size() == 2 &&
	           Take(loaded.scene->PersistentId(loaded.scene->RootGameObjects()[0])) == secondId,
	       "Scene root identity or order was lost");
	const auto restoredFirst = Take(loaded.scene->GameObjectById(firstId));
	const auto restoredChild = Take(loaded.scene->GameObjectById(childId));
	Expect(Take(loaded.scene->ParentOf(restoredChild)) == restoredFirst &&
	           Take(loaded.scene->GetLocalTransform(restoredChild)).position.x == 2.0f &&
	           !Take(loaded.scene->IsEnabled(restoredChild)),
	       "Scene hierarchy, transform, or enabled state was lost");
	Expect(Take(loaded.scene->ComponentPersistentId(loaded.scene->ActiveCamera())) == cameraId &&
	           loaded.scene->AmbientLightColor().green == 0.4f && loaded.scene->AmbientLightIntensity() == 0.25f &&
	           !Take(loaded.scene->IsComponentEnabled(Take(loaded.scene->ComponentsOf(restoredChild))[0])),
	       "Scene camera, ambient light, or component enabled state was lost");
	Expect(Take(loaded.scene->ComponentsOf(Take(loaded.scene->GameObjectById(secondId)))).size() == 3 &&
	           loaded.scene->GetComponent(Take(loaded.scene->ComponentByPersistentId(sunId))) != nullptr,
	       "Scene components were not restored");
	const auto restoredCamera = loaded.scene->FindComponent<Ursa::CameraComponent>(restoredFirst);
	const auto frame = Take(loaded.scene->CollectRenderData(1.0f));
	Expect(restoredCamera && restoredCamera->verticalFovRadians == 0.9f && restoredCamera->farPlane == 80.0f &&
	           frame.boxes.size() == 1 && frame.boxes[0].material &&
	           frame.boxes[0].material->Parameters().baseColor.blue == 0.8f && frame.lighting.directional.size() == 1 &&
	           frame.lighting.directional[0].intensity == 2.0f && frame.lighting.point.size() == 2 &&
	           frame.lighting.point[0].range == 7.0f,
	       "Built-in component fields or render snapshot changed after load");

	nlohmann::json document;
	{
		std::ifstream input(path);
		input >> document;
	}
	const auto originalId = document["gameObjects"][1]["id"];
	document["gameObjects"][1]["id"] = document["gameObjects"][0]["id"];
	const auto duplicate = root / "duplicate.ursascene";
	{
		std::ofstream output(duplicate);
		output << document;
	}
	Ursa::ComponentLoadContext duplicateContext(*session);
	auto repeated = Ursa::LoadSceneResource(duplicate, registry, duplicateContext);
	Expect(!repeated && repeated.error.find("Duplicate GameObject ID") != Ursa::String::npos,
	       "Duplicate GameObject ID was accepted");
	document["gameObjects"][1]["id"] = originalId;
	document["gameObjects"][1]["parentId"] = Ursa::ToStdString(Ursa::ResourceId::Generate().ToString());
	const auto missing = root / "missing.ursascene";
	{
		std::ofstream output(missing);
		output << document;
	}
	Ursa::ComponentLoadContext missingContext(*session);
	auto dangling = Ursa::LoadSceneResource(missing, registry, missingContext);
	Expect(!dangling && dangling.error.find("Missing parent") != Ursa::String::npos, "Missing parent ID was accepted");
	std::filesystem::remove(path);
	std::filesystem::remove(duplicate);
	std::filesystem::remove(missing);
	std::filesystem::remove(materialPath);
}

struct ProbeCounters
{
	int starts = 0;
	int removals = 0;
};

class ProbeComponent final : public Ursa::Component
{
public:
	explicit ProbeComponent(ProbeCounters& probeCounters) : counters(probeCounters)
	{
	}

	Ursa::Result<void> OnStart(const Ursa::FrameContext&) override
	{
		++counters.starts;
		return {};
	}

	void OnRemove() override
	{
		++counters.removals;
	}

private:
	ProbeCounters& counters;
};

void TestReload(const std::filesystem::path& root)
{
	ProbeCounters counters;
	Ursa::ComponentRegistry registry;
	Ursa::ComponentCodec codec;
	codec.type = "test.Probe";
	codec.matches = [](const Ursa::Component& component)
	{ return dynamic_cast<const ProbeComponent*>(&component) != nullptr; };
	codec.save = [](const Ursa::Component&, const Ursa::ComponentSaveContext&)
	{ return Ursa::Result<nlohmann::json>::Success(nlohmann::json::object()); };
	codec.create =
	    [](Ursa::Scene& scene, Ursa::GameObject::Ptr node, const nlohmann::json&, Ursa::ComponentLoadContext& context)
	{
		auto service = context.Service<ProbeCounters>();
		if (!service)
		{
			return Ursa::Result<Ursa::ComponentId>::Failure(service.error);
		}
		return scene.AddComponent<ProbeComponent>(node, *service.value);
	};
	done(registry.RegisterCodec(std::move(codec)));
	auto active = std::make_unique<Ursa::Scene>();
	const auto node = Take(active->CreateGameObject());
	Take(active->AddComponent<ProbeComponent>(node, counters));
	const auto path = root / "reload.ursascene";
	done(Ursa::SaveSceneResource(path, Ursa::ResourceId::Generate(), *active, registry));
	done(Ursa::WriteResourceMetadata(path));
	auto session = Take(Ursa::ResourceSession::Open({root}));
	Ursa::ComponentLoadContext context(*session);
	context.Provide(counters);
	auto* original = active.get();
	const auto invalid = root / "invalid.ursascene";
	{
		std::ofstream output(invalid);
		output << "{broken";
	}
	const auto failed = Ursa::ReloadSceneResource(invalid, registry, context, active);
	Expect(!failed && active.get() == original && counters.removals == 0, "Failed reload discarded the active scene");
	done(Ursa::ReloadSceneResource(path, registry, context, active));
	Expect(active.get() != original && counters.removals == 1 && counters.starts == 0,
	       "Successful reload did not replace the scene or started components early");
	active->BeginFrame();
	done(active->Update({}));
	Expect(counters.starts == 1, "Reloaded component did not start in the normal frame");
	active.reset();
	std::filesystem::remove(path);
	std::filesystem::remove(invalid);
}
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::temp_directory_path() /
		                  ("UrsaScenePersistenceTests-" + Ursa::ToStdString(Ursa::ResourceId::Generate().ToString()));
		std::filesystem::create_directory(root);
		const auto reset = [&]
		{
			std::filesystem::remove_all(root);
			std::filesystem::create_directory(root);
		};
		TestScene(root);
		reset();
		TestReload(root);
		reset();
		auto session = Take(Ursa::ResourceSession::Open({root}));
		RoundTripScene(root / "docs.ursascene", *session);
		std::filesystem::remove(root / "docs.ursascene");
		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
