#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Serialization/JsonInterop.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <cmath>
#include <functional>
#include <limits>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;
constexpr Ursa::Size maxSceneGameObjects = 100000;
constexpr Ursa::Size maxSceneComponents = 100000;

Json ColorJson(Color value)
{
	return Json::array({value.red, value.green, value.blue, value.alpha});
}

Json TransformJson(LocalTransform value)
{
	return {{"position", Json::array({value.position.x, value.position.y, value.position.z})},
	        {"rotation", Json::array({value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w})},
	        {"scale", Json::array({value.scale.x, value.scale.y, value.scale.z})}};
}

Result<Vector<Ursa::Float32>> ReadFloatArray(const Json& object, const char* field, Ursa::Size count,
                                             const String& context)
{
	auto value = object.find(field);
	if (value == object.end() || !value->is_array() || value->size() != count)
	{
		return Result<Vector<Ursa::Float32>>::Failure("Invalid " + context + "." + field);
	}
	Vector<Ursa::Float32> numbers;
	numbers.reserve(count);
	for (const auto& number : *value)
	{
		if (!number.is_number())
		{
			return Result<Vector<Ursa::Float32>>::Failure("Invalid number in " + context + "." + field);
		}
		const Ursa::Float64 converted = number.get<Ursa::Float64>();
		if (!std::isfinite(converted) || std::abs(converted) > std::numeric_limits<Ursa::Float32>::max())
		{
			return Result<Vector<Ursa::Float32>>::Failure("Out-of-range number in " + context + "." + field);
		}
		numbers.push_back(static_cast<Ursa::Float32>(converted));
	}
	return Result<Vector<Ursa::Float32>>::Success(std::move(numbers));
}

Result<ResourceId> ReadId(const Json& object, const char* field, const String& context)
{
	auto value = object.find(field);
	if (value == object.end() || !value->is_string())
	{
		return Result<ResourceId>::Failure("Invalid or missing " + context + "." + field);
	}
	auto id = ResourceId::Parse(value->get<String>());
	if (!id)
	{
		return Result<ResourceId>::Failure("Invalid " + context + "." + field + ": " + id.error);
	}
	return id;
}

struct GameObjectRecord
{
	ResourceId id;
	ResourceId parentId;
	String name;
	LocalTransform local;
	bool enabled = true;
	Json components;
};

Result<GameObjectRecord> ParseGameObject(const Json& item, const String& context)
{
	if (!item.is_object())
	{
		return Result<GameObjectRecord>::Failure("Invalid node object in " + context);
	}
	GameObjectRecord record;
	auto id = ReadId(item, "id", context);
	if (!id)
	{
		return Result<GameObjectRecord>::Failure(id.error);
	}
	record.id = id.value;
	const auto name = item.find("name");
	if (name != item.end())
	{
		if (!name->is_string())
		{
			return Result<GameObjectRecord>::Failure("Invalid " + context + ".name");
		}
		record.name = name->get<String>();
		if (record.name.size() > 256 || record.name.find('\0') != String::npos)
		{
			return Result<GameObjectRecord>::Failure("Invalid " + context + ".name");
		}
	}
	auto parentId = item.find("parentId");
	if (parentId == item.end())
	{
		return Result<GameObjectRecord>::Failure("Missing " + context + ".parentId");
	}
	if (!parentId->is_null())
	{
		if (!parentId->is_string())
		{
			return Result<GameObjectRecord>::Failure("Invalid " + context + ".parentId");
		}
		auto parsed = ResourceId::Parse(parentId->get<String>());
		if (!parsed)
		{
			return Result<GameObjectRecord>::Failure("Invalid " + context + ".parentId: " + parsed.error);
		}
		record.parentId = parsed.value;
	}
	auto localTransform = item.find("localTransform");
	if (localTransform == item.end() || !localTransform->is_object())
	{
		return Result<GameObjectRecord>::Failure("Invalid " + context + ".localTransform");
	}
	const auto position = ReadFloatArray(*localTransform, "position", 3, context + ".localTransform");
	const auto rotation = ReadFloatArray(*localTransform, "rotation", 4, context + ".localTransform");
	const auto scale = ReadFloatArray(*localTransform, "scale", 3, context + ".localTransform");
	if (!position || !rotation || !scale)
	{
		return Result<GameObjectRecord>::Failure(!position ? position.error : !rotation ? rotation.error : scale.error);
	}
	record.local.position = {position.value[0], position.value[1], position.value[2]};
	record.local.rotation = {rotation.value[0], rotation.value[1], rotation.value[2], rotation.value[3]};
	record.local.scale = {scale.value[0], scale.value[1], scale.value[2]};
	auto enabled = item.find("enabled");
	if (enabled == item.end() || !enabled->is_boolean())
	{
		return Result<GameObjectRecord>::Failure("Invalid " + context + ".enabled");
	}
	record.enabled = enabled->get<bool>();
	auto components = item.find("components");
	if (components == item.end() || !components->is_array())
	{
		return Result<GameObjectRecord>::Failure("Invalid " + context + ".components");
	}
	record.components = *components;
	return Result<GameObjectRecord>::Success(std::move(record));
}
} // namespace

Result<Json> EncodeSceneDocument(const std::filesystem::path& path, ResourceId id, const Scene& scene,
                                 const ComponentRegistry& registry, const MaterialIdResolver& materialId,
                                 const GameObject::Ptr& subtree)
{
	if (!id || path.extension() != ".ursascene")
	{
		return Result<Json>::Failure("Invalid scene resource ID or .ursascene path: " + PathToUrsaString(path));
	}
	if (subtree && !scene.Contains(subtree))
	{
		return Result<Json>::Failure("Invalid scene subtree root at " + PathToUrsaString(path));
	}
	Json gameObjects = Json::array();
	const ComponentSaveContext context{&scene, materialId};
	std::function<Result<void>(const GameObject::Ptr&)> append = [&](const GameObject::Ptr& object) -> Result<void>
	{
		if (gameObjects.size() >= maxSceneGameObjects)
		{
			return Result<void>::Failure("Scene node limit exceeded at " + PathToUrsaString(path));
		}
		const ResourceId objectId = object->Id();
		const GameObject::Ptr parentObject = object->Parent();
		const auto local = object->GetLocalTransform();
		const auto name = object->Name();
		const auto enabled = object->IsEnabled();
		const auto components = scene.ComponentsOf(object);
		if (!objectId || !local || !name || !enabled || !components)
		{
			return Result<void>::Failure("Cannot inspect GameObject at " + PathToUrsaString(path));
		}
		Json componentData = Json::array();
		for (ComponentId componentId : components.value)
		{
			auto codec = registry.FindFor(*scene.GetComponent(componentId));
			if (!codec)
			{
				return Result<void>::Failure("Cannot save component on GameObject " + objectId.ToString() + " in " +
				                             PathToUrsaString(path) + ": " + codec.error);
			}
			auto data = codec.value->save(*scene.GetComponent(componentId), context);
			if (!data)
			{
				return Result<void>::Failure("Cannot save component " + codec.value->type + " on GameObject " +
				                             objectId.ToString() + " in " + PathToUrsaString(path) + ": " + data.error);
			}
			const auto persistent = scene.ComponentPersistentId(componentId);
			const auto active = scene.IsComponentEnabled(componentId);
			if (!persistent || !active)
			{
				return Result<void>::Failure("Cannot inspect component on GameObject " + objectId.ToString());
			}
			componentData.push_back({{"id", persistent.value.ToString()},
			                         {"type", codec.value->type},
			                         {"version", codec.value->version},
			                         {"enabled", active.value},
			                         {"data", std::move(data.value)}});
		}
		gameObjects.push_back({{"id", objectId.ToString()},
		                       {"name", name.value},
		                       {"parentId", parentObject ? Json(parentObject->Id().ToString()) : Json(nullptr)},
		                       {"localTransform", TransformJson(local.value)},
		                       {"enabled", enabled.value},
		                       {"components", std::move(componentData)}});
		for (const GameObject::Ptr& child : object->Children())
		{
			auto result = append(child);
			if (!result)
			{
				return result;
			}
		}
		return {};
	};
	const auto roots = subtree ? Vector<GameObject::Ptr>{subtree} : scene.RootGameObjects();
	for (const GameObject::Ptr& root : roots)
	{
		auto result = append(root);
		if (!result)
		{
			return Result<Json>::Failure(result.error);
		}
	}
	Json activeCamera = nullptr;
	if (scene.ActiveCamera())
	{
		auto cameraId = scene.ComponentPersistentId(scene.ActiveCamera());
		if (!cameraId)
		{
			return Result<Json>::Failure("Invalid active camera in " + PathToUrsaString(path) + ": " + cameraId.error);
		}
		activeCamera = cameraId.value.ToString();
	}
	const auto ambient = scene.AmbientLightColor();
	const Json document{{"format", "UrsaScene"},
	                    {"version", resourceFormatVersion},
	                    {"sceneGraphVersion", sceneGraphFormatVersion},
	                    {"id", id.ToString()},
	                    {"ambient", {{"color", ColorJson(ambient)}, {"intensity", scene.AmbientLightIntensity()}}},
	                    {"activeCamera", std::move(activeCamera)},
	                    {"gameObjects", std::move(gameObjects)}};
	return Result<Json>::Success(document);
}

Result<void> SaveSceneResource(const std::filesystem::path& path, ResourceId id, const Scene& scene,
                               const ComponentRegistry& registry, const MaterialIdResolver& materialId)
{
	auto document = EncodeSceneDocument(path, id, scene, registry, materialId);
	return document ? WriteJsonResource(path, document.value) : Result<void>::Failure(document.error);
}

Result<LoadedSceneResource> DecodeSceneDocument(const std::filesystem::path& path, const Json& document,
                                                const ComponentRegistry& registry, ComponentLoadContext& context)
{
	context.ClearMappings();
	const auto sceneGraphVersion = document.find("sceneGraphVersion");
	if (sceneGraphVersion == document.end() || !sceneGraphVersion->is_number_unsigned() ||
	    sceneGraphVersion->get<Ursa::UInt32>() != sceneGraphFormatVersion)
	{
		return Result<LoadedSceneResource>::Failure("Unsupported or missing GameObject Scene format version at " +
		                                            PathToUrsaString(path));
	}
	auto gameObjects = document.find("gameObjects");
	if (gameObjects == document.end() || !gameObjects->is_array() || gameObjects->size() > maxSceneGameObjects)
	{
		return Result<LoadedSceneResource>::Failure("Invalid GameObject list at " + PathToUrsaString(path));
	}
	Vector<GameObjectRecord> records;
	Set<String> nodeIds;
	Set<String> componentIds;
	Ursa::Size componentCount = 0;
	for (Ursa::Size index = 0; index < gameObjects->size(); ++index)
	{
		auto record = ParseGameObject((*gameObjects)[index], PathToUrsaString(path) + ".gameObjects[" +
		                                                         ToUrsaString(std::to_string(index)) + "]");
		if (!record)
		{
			return Result<LoadedSceneResource>::Failure(record.error);
		}
		if (!nodeIds.insert(record.value.id.ToString()).second)
		{
			return Result<LoadedSceneResource>::Failure("Duplicate GameObject ID " + record.value.id.ToString() +
			                                            " in " + PathToUrsaString(path));
		}
		componentCount += record.value.components.size();
		if (componentCount > maxSceneComponents)
		{
			return Result<LoadedSceneResource>::Failure("Scene component limit exceeded at " + PathToUrsaString(path));
		}
		records.push_back(std::move(record.value));
	}

	auto scene = std::make_unique<Scene>();
	for (const auto& record : records)
	{
		auto created = scene->CreateGameObject(record.name);
		if (!created)
		{
			return Result<LoadedSceneResource>::Failure(created.error);
		}
		auto assigned = scene->AssignGameObjectId(created.value, record.id);
		if (!assigned)
		{
			return Result<LoadedSceneResource>::Failure(assigned.error);
		}
		auto mapped = context.MapGameObject(record.id, created.value);
		if (!mapped)
		{
			return Result<LoadedSceneResource>::Failure(mapped.error + " in " + PathToUrsaString(path));
		}
		auto placed = created.value->SetLocalTransform(record.local);
		if (!placed)
		{
			return Result<LoadedSceneResource>::Failure(placed.error);
		}
		auto enabled = created.value->SetEnabled(record.enabled);
		if (!enabled)
		{
			return Result<LoadedSceneResource>::Failure(enabled.error);
		}
	}
	for (const auto& record : records)
	{
		if (!record.parentId)
		{
			continue;
		}
		auto node = context.GameObjectById(record.id);
		auto parentId = context.GameObjectById(record.parentId);
		if (!parentId)
		{
			return Result<LoadedSceneResource>::Failure("Missing parentId " + record.parentId.ToString() +
			                                            " for GameObject " + record.id.ToString() + " in " +
			                                            PathToUrsaString(path));
		}
		auto reparented = scene->SetParent(node.value, parentId.value);
		if (!reparented)
		{
			return Result<LoadedSceneResource>::Failure("Invalid parentId for GameObject " + record.id.ToString() +
			                                            " in " + PathToUrsaString(path) + ": " + reparented.error);
		}
	}

	struct PendingBind
	{
		const ComponentCodec* codec;
		ComponentId component;
		Json data;
		String location;
	};

	Vector<PendingBind> binds;
	for (const auto& record : records)
	{
		const auto owner = context.GameObjectById(record.id);
		for (Ursa::Size index = 0; index < record.components.size(); ++index)
		{
			const auto& item = record.components[index];
			const auto location = PathToUrsaString(path) + ".node[" + record.id.ToString() + "].components[" +
			                      ToUrsaString(std::to_string(index)) + "]";
			if (!item.is_object())
			{
				return Result<LoadedSceneResource>::Failure("Invalid component at " + location);
			}
			auto id = ReadId(item, "id", location);
			if (!id)
			{
				return Result<LoadedSceneResource>::Failure(id.error);
			}
			if (!componentIds.insert(id.value.ToString()).second)
			{
				return Result<LoadedSceneResource>::Failure("Duplicate component ID " + id.value.ToString() + " at " +
				                                            location);
			}
			auto type = item.find("type");
			auto version = item.find("version");
			auto enabled = item.find("enabled");
			auto data = item.find("data");
			if (type == item.end() || !type->is_string() || version == item.end() || !version->is_number_unsigned() ||
			    enabled == item.end() || !enabled->is_boolean() || data == item.end())
			{
				return Result<LoadedSceneResource>::Failure("Invalid component metadata at " + location);
			}
			const auto name = type->get<String>();
			const auto* codec = registry.Find(name);
			if (!codec || !codec->create)
			{
				return Result<LoadedSceneResource>::Failure("Unregistered component type " + name + " at " + location);
			}
			if (version->get<Ursa::UInt64>() != codec->version)
			{
				return Result<LoadedSceneResource>::Failure("Unsupported component version for " + name + " at " +
				                                            location);
			}
			auto created = codec->create(*scene, owner.value, *data, context);
			if (!created)
			{
				return Result<LoadedSceneResource>::Failure("Cannot create component " + name + " at " + location +
				                                            ": " + created.error);
			}
			auto assigned = scene->AssignComponentPersistentId(created.value, id.value);
			if (!assigned)
			{
				return Result<LoadedSceneResource>::Failure(assigned.error + " at " + location);
			}
			auto mapped = context.MapComponent(id.value, created.value);
			if (!mapped)
			{
				return Result<LoadedSceneResource>::Failure(mapped.error + " at " + location);
			}
			auto changed = scene->SetComponentEnabled(created.value, enabled->get<bool>());
			if (!changed)
			{
				return Result<LoadedSceneResource>::Failure(changed.error + " at " + location);
			}
			binds.push_back({codec, created.value, *data, location});
		}
	}
	for (const auto& pending : binds)
	{
		if (pending.codec->bind)
		{
			auto bound = pending.codec->bind(*scene, pending.component, pending.data, context);
			if (!bound)
			{
				return Result<LoadedSceneResource>::Failure("Cannot bind component at " + pending.location + ": " +
				                                            bound.error);
			}
		}
	}

	auto ambient = document.find("ambient");
	if (ambient == document.end() || !ambient->is_object())
	{
		return Result<LoadedSceneResource>::Failure("Invalid scene ambient light at " + PathToUrsaString(path));
	}
	const auto color = ReadFloatArray(*ambient, "color", 4, PathToUrsaString(path) + ".ambient");
	auto intensity = ambient->find("intensity");
	if (!color || intensity == ambient->end() || !intensity->is_number())
	{
		return Result<LoadedSceneResource>::Failure("Invalid scene ambient light at " + PathToUrsaString(path));
	}
	const Ursa::Float64 light = intensity->get<Ursa::Float64>();
	if (!std::isfinite(light) || light < 0.0 || light > std::numeric_limits<Ursa::Float32>::max())
	{
		return Result<LoadedSceneResource>::Failure("Invalid scene ambient intensity at " + PathToUrsaString(path));
	}
	auto lit = scene->SetAmbientLight({color.value[0], color.value[1], color.value[2], color.value[3]},
	                                  static_cast<Ursa::Float32>(light));
	if (!lit)
	{
		return Result<LoadedSceneResource>::Failure(lit.error + " at " + PathToUrsaString(path));
	}
	auto activeCamera = document.find("activeCamera");
	if (activeCamera == document.end())
	{
		return Result<LoadedSceneResource>::Failure("Missing scene activeCamera at " + PathToUrsaString(path));
	}
	if (!activeCamera->is_null())
	{
		auto cameraId = ReadId(document, "activeCamera", PathToUrsaString(path));
		if (!cameraId)
		{
			return Result<LoadedSceneResource>::Failure(cameraId.error);
		}
		auto camera = context.Component(cameraId.value);
		if (!camera)
		{
			return Result<LoadedSceneResource>::Failure("Missing active camera component " + cameraId.value.ToString() +
			                                            " at " + PathToUrsaString(path));
		}
		auto selected = scene->SetActiveCamera(camera.value);
		if (!selected)
		{
			return Result<LoadedSceneResource>::Failure(selected.error + " at " + PathToUrsaString(path));
		}
	}
	auto id = ReadId(document, "id", PathToUrsaString(path));
	if (!id)
	{
		return Result<LoadedSceneResource>::Failure(id.error);
	}
	return Result<LoadedSceneResource>::Success({id.value, std::move(scene)});
}

Result<LoadedSceneResource> LoadSceneResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                              ComponentLoadContext& context)
{
	auto document = ReadJsonResource(path, ResourceKind::Scene);
	if (!document)
	{
		return Result<LoadedSceneResource>::Failure(document.error);
	}
	if (document.value.contains("instances"))
	{
		return Result<LoadedSceneResource>::Failure("Linked scene at " + PathToUrsaString(path) +
		                                            " must be loaded with loadLinkedSceneResource");
	}
	return DecodeSceneDocument(path, document.value, registry, context);
}

Result<void> ReloadSceneResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                 ComponentLoadContext& context, std::unique_ptr<Scene>& activeScene)
{
	auto candidate = LoadSceneResource(path, registry, context);
	if (!candidate)
	{
		return Result<void>::Failure(candidate.error);
	}
	activeScene = std::move(candidate.value.scene);
	return {};
}
} // namespace Ursa
