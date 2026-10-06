#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Scene/Component.hpp>
#include <Scene/Scene.hpp>
#include <Serialization/ResourceSession.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <typeindex>

namespace Ursa
{
using MaterialIdResolver = std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>;

struct ComponentSaveContext
{
	const Scene* scene = nullptr;
	MaterialIdResolver materialId;

	Result<ResourceId> GetGameObjectId(const GameObject::Ptr& object) const
	{
		return object ? Result<ResourceId>::Success(object->Id())
		              : Result<ResourceId>::Failure("Cannot query the ID of an empty GameObject handle");
	}

	Result<ResourceId> GetComponentId(ComponentId component) const
	{
		return scene ? scene->ComponentPersistentId(component)
		             : Result<ResourceId>::Failure("No Scene context is available for this component reference");
	}
};

class ComponentLoadContext final
{
public:
	explicit ComponentLoadContext(ResourceSession& resourceSession) : resources(resourceSession)
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

	Result<GameObject::Ptr> GameObjectById(ResourceId id) const;

	Result<ComponentId> Component(ResourceId id) const;

	Result<void> MapGameObject(ResourceId id, GameObject::Ptr object);

	Result<void> MapComponent(ResourceId id, ComponentId component);

	Result<::Ursa::Component*> ComponentObjectById(ResourceId id) const;

	Result<void> MapComponentObject(ResourceId id, ::Ursa::Component* component);

	void ClearMappings();

private:
	ResourceSession& resources;
	Map<std::type_index, void*> services;
	Map<String, GameObject::Ptr> gameObjects;
	Map<String, ComponentId> components;
	Map<String, ::Ursa::Component*> componentObjects;
};

struct ComponentCodec
{
	String type;
	Ursa::UInt32 version = 1;
	std::function<bool(const Component&)> matches;
	std::function<Result<nlohmann::json>(const Component&, const ComponentSaveContext&)> save;
	std::function<Result<ComponentId>(Scene&, const GameObject::Ptr&, const nlohmann::json&, ComponentLoadContext&)>
	    create;
	std::function<Result<void>(Scene&, ComponentId, const nlohmann::json&, ComponentLoadContext&)> bind;
	std::function<Result<Component*>(const GameObject::Ptr&, const nlohmann::json&, ComponentLoadContext&)>
	    createDetached;
	std::function<Result<void>(Component*, const nlohmann::json&, ComponentLoadContext&)> bindDetached;
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
