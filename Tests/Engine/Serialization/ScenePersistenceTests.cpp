#include <MeshFixtures.hpp>
#include <ResourceFixtures.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Containers.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/Archive.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
using namespace Ursa;

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

class FakeManagedBehaviourRuntime final : public Ursa::Scripting::ManagedBehaviourRuntime
{
public:
	Result<ComponentId> AddExisting(const GameObject::Ptr&, const String&, UInt64) override
	{
		return Result<ComponentId>::Failure("Not used by the serialization fixture");
	}

	Result<ComponentId> CreateAndAdd(const GameObject::Ptr&, const String&) override
	{
		return Result<ComponentId>::Failure("Not used by the serialization fixture");
	}

	Result<void> Dispatch(UInt64, Ursa::Scripting::BehaviourEvent, const FrameContext*, Component*) override
	{
		return {};
	}

	Result<void> Destroy(UInt64) override
	{
		return {};
	}

	Result<String> SerializeFields(UInt64) override
	{
		return Result<String>::Success(
		    R"({"FormatVersion":1,"ScriptType":"test.migration","Version":2,"Fields":{"newName":31},"FormerFieldNames":{"newName":["oldName"]}})");
	}
};

void TestScriptFieldMigrationAndUnknownPreservation()
{
	FakeManagedBehaviourRuntime runtime;
	Ursa::Scripting::ScriptBehaviourComponent component(runtime, "MigrationProbeBehaviour", 1);
	Ursa::ArchiveWriter input;
	done(input.Write("typeName", Ursa::String("MigrationProbeBehaviour")));
	auto scriptPayload = Take(input.Object("scriptPayload"));
	done(Ursa::ArchiveWriteJson(
	    scriptPayload,
	    nlohmann::json::parse(
	        R"({"FormatVersion":1,"ScriptType":"test.migration","Version":2,"Fields":{"oldName":31,"removedField":"keep me"}})")));
	auto encoded = Take(Ursa::ArchiveToJson(input));
	auto reader = Take(Ursa::ArchiveFromJson(encoded));
	Ursa::SerializationContext context;
	done(component.Deserialize(reader, context));
	Ursa::ArchiveWriter output;
	done(component.Serialize(output));
	const auto saved = Take(Ursa::ArchiveToJson(output));
	const auto payload = saved.at("scriptPayload");
	Expect(payload.at("Fields").at("newName").get<int>() == 31 &&
	           payload.at("Fields").at("removedField").get<std::string>() == "keep me" &&
	           !payload.at("Fields").contains("oldName"),
	       "Script field migration did not keep the migrated value and unknown saved field");
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
	done(scene.SetActive(child, false));
	done(scene.SetAmbientLight({0.3f, 0.4f, 0.5f, 1.0f}, 0.25f));
	const auto camera = Take(scene.AddComponent<Ursa::CameraComponent>(first));
	auto* cameraObject = static_cast<Ursa::CameraComponent*>(scene.GetComponent(camera));
	cameraObject->verticalFovRadians = 0.9f;
	cameraObject->nearPlane = 0.2f;
	cameraObject->farPlane = 80.0f;
	done(scene.SetActiveCamera(camera));
	const auto box = Take(Ursa::Test::AddRenderer(scene, child, Ursa::Color{0.9f, 0.1f, 0.2f, 1.0f}));
	done(scene.SetComponentEnabled(box, false));
	const auto sun = Take(scene.AddComponent<Ursa::DirectionalLightComponent>(second));
	auto* sunObject = static_cast<Ursa::DirectionalLightComponent*>(scene.GetComponent(sun));
	done(sunObject->SetColor({0.8f, 0.7f, 0.6f, 1.0f}));
	done(sunObject->SetIntensity(2.0f));
	const auto lamp = Take(scene.AddComponent<Ursa::PointLightComponent>(second));
	auto* lampObject = static_cast<Ursa::PointLightComponent*>(scene.GetComponent(lamp));
	done(lampObject->SetRange(7.0f));
	Take(scene.AddComponent<Ursa::PointLightComponent>(second));
	const auto script =
	    Take(scene.AddComponent<Ursa::Scripting::ScriptBehaviourComponent>(first, "Demo.MoveWithInput"));
	auto* scriptObject = static_cast<Ursa::Scripting::ScriptBehaviourComponent*>(scene.GetComponent(script));
	const auto savedChildId = Take(scene.PersistentId(child));
	const auto savedCameraId = Take(scene.ComponentPersistentId(camera));
	nlohmann::json scriptPayload = nlohmann::json::object();
	scriptPayload["FormatVersion"] = 1u;
	scriptPayload["ScriptType"] = "demo.move-with-input";
	scriptPayload["Version"] = 1u;
	scriptPayload["Fields"] = nlohmann::json::object();
	scriptPayload["Fields"]["step"] = 2.5;
	scriptPayload["Fields"]["targetObject"]["$ursaReference"]["Kind"] = "GameObject";
	scriptPayload["Fields"]["targetObject"]["$ursaReference"]["Id"] = Ursa::ToStdString(savedChildId.ToString());
	scriptPayload["Fields"]["targetCamera"]["$ursaReference"]["Kind"] = "Component";
	scriptPayload["Fields"]["targetCamera"]["$ursaReference"]["Id"] = Ursa::ToStdString(savedCameraId.ToString());
	scriptObject->SetSerializedPayload(Ursa::ToUrsaString(scriptPayload.dump()));
	Ursa::ArchiveWriter scriptArchive;
	done(scriptObject->Serialize(scriptArchive));
	Ursa::Vector<Ursa::UInt8> scriptArchiveBytes = Take(Ursa::BinaryArchive::Encode(scriptArchive));
	Ursa::ArchiveReader scriptArchiveReader = Take(Ursa::BinaryArchive::Decode(scriptArchiveBytes));
	Ursa::Scripting::ScriptBehaviourComponent archiveRestored(Ursa::String{});
	Ursa::SerializationContext scriptSerializationContext;
	done(archiveRestored.Deserialize(scriptArchiveReader, scriptSerializationContext));
	Expect(archiveRestored.TypeName() == "Demo.MoveWithInput" && archiveRestored.HasSerializedPayload() &&
	           Ursa::ToStdString(archiveRestored.SerializedPayload()).find("step") != std::string::npos,
	       "Binary Archive did not preserve script identity and field payload");
	auto material = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::Unlit);
	done(material->SetBaseColor({0.2f, 0.5f, 0.8f, 1.0f}));
	const auto materialId = Ursa::ResourceId::Generate();
	const auto materialPath = root / "box.ursamat";
	done(Ursa::SaveMaterialResource(materialPath, materialId, *material, {}));
	done(Ursa::WriteResourceMetadata(materialPath));
	const auto visibleBox = Take(Ursa::Test::AddRenderer(scene, first));
	done(static_cast<Ursa::MeshRendererComponent*>(scene.GetComponent(visibleBox))->SetSharedMaterial(material));
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
	           !Take(loaded.scene->ActiveSelf(restoredChild)),
	       "Scene hierarchy, transform, or enabled state was lost");
	Expect(Take(loaded.scene->ComponentPersistentId(loaded.scene->ActiveCamera())) == cameraId &&
	           loaded.scene->AmbientLightColor().green == 0.4f && loaded.scene->AmbientLightIntensity() == 0.25f &&
	           !Take(loaded.scene->IsComponentEnabled(
	               loaded.scene->FindComponent<Ursa::MeshRendererComponent>(restoredChild)->Id())),
	       "Scene camera, ambient light, or component enabled state was lost");
	Expect(Take(loaded.scene->ComponentsOf(Take(loaded.scene->GameObjectById(secondId)))).size() == 3 &&
	           loaded.scene->GetComponent(Take(loaded.scene->ComponentByPersistentId(sunId))) != nullptr,
	       "Scene components were not restored");
	auto restoredComponents = Take(loaded.scene->ComponentsOf(restoredFirst));
	bool restoredScript = false;
	for (Ursa::ComponentId componentId : restoredComponents)
	{
		auto* component = loaded.scene->GetComponent(componentId);
		auto* scriptComponent = dynamic_cast<Ursa::Scripting::ScriptBehaviourComponent*>(component);
		if (scriptComponent)
		{
			restoredScript = scriptComponent->TypeName() == "Demo.MoveWithInput" &&
			                 scriptComponent->HasSerializedPayload() &&
			                 Ursa::ToStdString(scriptComponent->SerializedPayload()).find("demo.move-with-input") !=
			                     std::string::npos &&
			                 Ursa::ToStdString(scriptComponent->SerializedPayload()).find("step") != std::string::npos;
		}
	}
	Expect(restoredScript, "Scene codec did not preserve a managed script type and field payload");
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

	Ursa::Result<void> Start(const Ursa::FrameContext&) override
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
		TestScriptFieldMigrationAndUnknownPreservation();
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
