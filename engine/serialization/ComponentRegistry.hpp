#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <scene/Scene.hpp>
#include <serialization/ResourceSession.hpp>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <typeindex>

namespace Ursa
{
using MaterialIdResolver = std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>;

struct ComponentSaveContext
{
	const Scene& scene;
	MaterialIdResolver materialId;

	Result<ResourceId> GetNodeId(NodeId node) const
	{
		return scene.NodePersistentId(node);
	}

	Result<ResourceId> GetComponentId(ComponentId component) const
	{
		return scene.ComponentPersistentId(component);
	}
};

class ComponentLoadContext final
{
public:
	explicit ComponentLoadContext(ResourceSession& resources) : resources(resources)
	{
	}

	ResourceSession& Resources() const
	{
		return resources;
	}

	template <class T> void Provide(T& service)
	{
		services[std::type_index(typeid(T))] = &service;
	}

	template <class T> Result<T*> Service() const
	{
		auto found = services.find(std::type_index(typeid(T)));
		if (found == services.end())
		{
			return Result<T*>::Failure("Missing runtime service " + String(typeid(T).name()));
		}
		return Result<T*>::Success(static_cast<T*>(found->second));
	}

	Result<NodeId> Node(ResourceId id) const;

	Result<ComponentId> Component(ResourceId id) const;

	Result<void> MapNode(ResourceId id, NodeId node);

	Result<void> MapComponent(ResourceId id, ComponentId component);

	void ClearMappings();

private:
	ResourceSession& resources;
	Map<std::type_index, void*> services;
	Map<String, NodeId> nodes;
	Map<String, ComponentId> components;
};

struct ComponentCodec
{
	String type;
	uint32_t version = 1;
	std::function<bool(const Component&)> matches;
	std::function<Result<nlohmann::json>(const Component&, const ComponentSaveContext&)> save;
	std::function<Result<ComponentId>(Scene&, NodeId, const nlohmann::json&, ComponentLoadContext&)> create;
	std::function<Result<void>(Scene&, ComponentId, const nlohmann::json&, ComponentLoadContext&)> bind;
};

class ComponentRegistry final
{
public:
	Result<void> RegisterCodec(ComponentCodec codec);

	Result<const ComponentCodec*> FindFor(const Component& component) const;

	const ComponentCodec* Find(const String& type) const;

private:
	Map<String, ComponentCodec> codecs;
};

Result<void> RegisterBuiltinComponents(ComponentRegistry& registry);
} // namespace Ursa
