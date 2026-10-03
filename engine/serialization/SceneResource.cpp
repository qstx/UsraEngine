#include <serialization/JsonInterop.hpp>
#include <serialization/SceneResource.hpp>
#include <serialization/JsonResource.hpp>
#include <cmath>
#include <functional>
#include <limits>
#include <set>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;
constexpr size_t maxSceneNodes = 100000;
constexpr size_t maxSceneComponents = 100000;

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

Result<Vector<float>> ReadFloatArray(const Json& object, const char* field, size_t count, const String& context)
{
	auto value = object.find(field);
	if (value == object.end() || !value->is_array() || value->size() != count)
	{
		return Result<Vector<float>>::Failure("Invalid " + context + "." + field);
	}
	Vector<float> numbers;
	numbers.reserve(count);
	for (const auto& number : *value)
	{
		if (!number.is_number())
		{
			return Result<Vector<float>>::Failure("Invalid number in " + context + "." + field);
		}
		const double converted = number.get<double>();
		if (!std::isfinite(converted) || std::abs(converted) > std::numeric_limits<float>::max())
		{
			return Result<Vector<float>>::Failure("Out-of-range number in " + context + "." + field);
		}
		numbers.push_back(static_cast<float>(converted));
	}
	return Result<Vector<float>>::Success(std::move(numbers));
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

struct NodeRecord
{
	ResourceId id;
	ResourceId parent;
	String label;
	LocalTransform local;
	bool enabled = true;
	Json components;
};

Result<NodeRecord> ParseNode(const Json& item, const String& context)
{
	if (!item.is_object())
	{
		return Result<NodeRecord>::Failure("Invalid node object in " + context);
	}
	NodeRecord record;
	auto id = ReadId(item, "id", context);
	if (!id)
	{
		return Result<NodeRecord>::Failure(id.error);
	}
	record.id = id.value;
	const auto label = item.find("label");
	if (label != item.end())
	{
		if (!label->is_string())
		{
			return Result<NodeRecord>::Failure("Invalid " + context + ".label");
		}
		record.label = label->get<String>();
		if (record.label.size() > 256 || record.label.find('\0') != String::npos)
		{
			return Result<NodeRecord>::Failure("Invalid " + context + ".label");
		}
	}
	auto parent = item.find("parent");
	if (parent == item.end())
	{
		return Result<NodeRecord>::Failure("Missing " + context + ".parent");
	}
	if (!parent->is_null())
	{
		if (!parent->is_string())
		{
			return Result<NodeRecord>::Failure("Invalid " + context + ".parent");
		}
		auto parsed = ResourceId::Parse(parent->get<String>());
		if (!parsed)
		{
			return Result<NodeRecord>::Failure("Invalid " + context + ".parent: " + parsed.error);
		}
		record.parent = parsed.value;
	}
	auto transform = item.find("transform");
	if (transform == item.end() || !transform->is_object())
	{
		return Result<NodeRecord>::Failure("Invalid " + context + ".transform");
	}
	const auto position = ReadFloatArray(*transform, "position", 3, context + ".transform");
	const auto rotation = ReadFloatArray(*transform, "rotation", 4, context + ".transform");
	const auto scale = ReadFloatArray(*transform, "scale", 3, context + ".transform");
	if (!position || !rotation || !scale)
	{
		return Result<NodeRecord>::Failure(!position ? position.error : !rotation ? rotation.error : scale.error);
	}
	record.local.position = {position.value[0], position.value[1], position.value[2]};
	record.local.rotation = {rotation.value[0], rotation.value[1], rotation.value[2], rotation.value[3]};
	record.local.scale = {scale.value[0], scale.value[1], scale.value[2]};
	auto enabled = item.find("enabled");
	if (enabled == item.end() || !enabled->is_boolean())
	{
		return Result<NodeRecord>::Failure("Invalid " + context + ".enabled");
	}
	record.enabled = enabled->get<bool>();
	auto components = item.find("components");
	if (components == item.end() || !components->is_array())
	{
		return Result<NodeRecord>::Failure("Invalid " + context + ".components");
	}
	record.components = *components;
	return Result<NodeRecord>::Success(std::move(record));
}
} // namespace

Result<Json> EncodeSceneDocument(const std::filesystem::path& path, ResourceId id, const Scene& scene,
                                 const ComponentRegistry& registry, const MaterialIdResolver& materialId,
                                 NodeId subtree)
{
	if (!id || path.extension() != ".ursascene")
	{
		return Result<Json>::Failure("Invalid scene resource ID or .ursascene path: " + PathToUrsaString(path));
	}
	if (subtree && !scene.Contains(subtree))
	{
		return Result<Json>::Failure("Invalid scene subtree root at " + PathToUrsaString(path));
	}
	Json nodes = Json::array();
	const ComponentSaveContext context{scene, materialId};
	std::function<Result<void>(NodeId)> append = [&](NodeId node) -> Result<void>
	{
		if (nodes.size() >= maxSceneNodes)
		{
			return Result<void>::Failure("Scene node limit exceeded at " + PathToUrsaString(path));
		}
		const auto nodeId = scene.NodePersistentId(node);
		const auto parent = scene.ParentOf(node);
		const auto local = scene.GetLocalTransform(node);
		const auto label = scene.NodeLabel(node);
		const auto enabled = scene.IsEnabled(node);
		const auto components = scene.ComponentsOf(node);
		if (!nodeId || !parent || !local || !label || !enabled || !components)
		{
			return Result<void>::Failure("Cannot inspect scene node at " + PathToUrsaString(path));
		}
		Json componentData = Json::array();
		for (ComponentId componentId : components.value)
		{
			auto codec = registry.FindFor(*scene.GetComponent(componentId));
			if (!codec)
			{
				return Result<void>::Failure("Cannot save component on node " + nodeId.value.ToString() + " in " +
				                             PathToUrsaString(path) + ": " + codec.error);
			}
			auto data = codec.value->save(*scene.GetComponent(componentId), context);
			if (!data)
			{
				return Result<void>::Failure("Cannot save component " + codec.value->type + " on node " +
				                             nodeId.value.ToString() + " in " + PathToUrsaString(path) + ": " +
				                             data.error);
			}
			const auto persistent = scene.ComponentPersistentId(componentId);
			const auto active = scene.IsComponentEnabled(componentId);
			if (!persistent || !active)
			{
				return Result<void>::Failure("Cannot inspect component on node " + nodeId.value.ToString());
			}
			componentData.push_back({{"id", persistent.value.ToString()},
			                         {"type", codec.value->type},
			                         {"version", codec.value->version},
			                         {"enabled", active.value},
			                         {"data", std::move(data.value)}});
		}
		const auto parentId = parent.value ? scene.NodePersistentId(parent.value) : Result<ResourceId>{};
		if (parent.value && !parentId)
		{
			return Result<void>::Failure(parentId.error);
		}
		nodes.push_back({{"id", nodeId.value.ToString()},
		                 {"label", label.value},
		                 {"parent", parent.value ? Json(parentId.value.ToString()) : Json(nullptr)},
		                 {"transform", TransformJson(local.value)},
		                 {"enabled", enabled.value},
		                 {"components", std::move(componentData)}});
		auto children = scene.ChildrenOf(node);
		if (!children)
		{
			return Result<void>::Failure(children.error);
		}
		for (NodeId child : children.value)
		{
			auto result = append(child);
			if (!result)
			{
				return result;
			}
		}
		return {};
	};
	const auto roots = subtree ? Vector<NodeId>{subtree} : scene.RootNodes();
	for (NodeId root : roots)
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
	                    {"id", id.ToString()},
	                    {"ambient", {{"color", ColorJson(ambient)}, {"intensity", scene.AmbientLightIntensity()}}},
	                    {"activeCamera", std::move(activeCamera)},
	                    {"nodes", std::move(nodes)}};
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
	auto nodes = document.find("nodes");
	if (nodes == document.end() || !nodes->is_array() || nodes->size() > maxSceneNodes)
	{
		return Result<LoadedSceneResource>::Failure("Invalid scene nodes at " + PathToUrsaString(path));
	}
	Vector<NodeRecord> records;
	Set<String> nodeIds;
	Set<String> componentIds;
	size_t componentCount = 0;
	for (size_t index = 0; index < nodes->size(); ++index)
	{
		auto record =
		    ParseNode((*nodes)[index], PathToUrsaString(path) + ".nodes[" + ToUrsaString(std::to_string(index)) + "]");
		if (!record)
		{
			return Result<LoadedSceneResource>::Failure(record.error);
		}
		if (!nodeIds.insert(record.value.id.ToString()).second)
		{
			return Result<LoadedSceneResource>::Failure("Duplicate node ID " + record.value.id.ToString() + " in " +
			                                            PathToUrsaString(path));
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
		auto created = scene->CreateNode();
		if (!created)
		{
			return Result<LoadedSceneResource>::Failure(created.error);
		}
		auto assigned = scene->AssignNodePersistentId(created.value, record.id);
		if (!assigned)
		{
			return Result<LoadedSceneResource>::Failure(assigned.error);
		}
		auto labeled = scene->SetNodeLabel(created.value, record.label);
		if (!labeled)
		{
			return Result<LoadedSceneResource>::Failure(labeled.error);
		}
		auto mapped = context.MapNode(record.id, created.value);
		if (!mapped)
		{
			return Result<LoadedSceneResource>::Failure(mapped.error + " in " + PathToUrsaString(path));
		}
		auto placed = scene->SetLocalTransform(created.value, record.local);
		if (!placed)
		{
			return Result<LoadedSceneResource>::Failure(placed.error);
		}
		auto enabled = scene->SetEnabled(created.value, record.enabled);
		if (!enabled)
		{
			return Result<LoadedSceneResource>::Failure(enabled.error);
		}
	}
	for (const auto& record : records)
	{
		if (!record.parent)
		{
			continue;
		}
		auto node = context.Node(record.id);
		auto parent = context.Node(record.parent);
		if (!parent)
		{
			return Result<LoadedSceneResource>::Failure("Missing parent " + record.parent.ToString() + " for node " +
			                                            record.id.ToString() + " in " + PathToUrsaString(path));
		}
		auto reparented = scene->SetParent(node.value, parent.value);
		if (!reparented)
		{
			return Result<LoadedSceneResource>::Failure("Invalid parent for node " + record.id.ToString() + " in " +
			                                            PathToUrsaString(path) + ": " + reparented.error);
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
		const auto owner = context.Node(record.id);
		for (size_t index = 0; index < record.components.size(); ++index)
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
			if (!codec)
			{
				return Result<LoadedSceneResource>::Failure("Unregistered component type " + name + " at " + location);
			}
			if (version->get<uint64_t>() != codec->version)
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
	const double light = intensity->get<double>();
	if (!std::isfinite(light) || light < 0.0 || light > std::numeric_limits<float>::max())
	{
		return Result<LoadedSceneResource>::Failure("Invalid scene ambient intensity at " + PathToUrsaString(path));
	}
	auto lit = scene->SetAmbientLight({color.value[0], color.value[1], color.value[2], color.value[3]},
	                                  static_cast<float>(light));
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
	auto document = ReadJsonResource(path, ResourceKind::scene);
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
