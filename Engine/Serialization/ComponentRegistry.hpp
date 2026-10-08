#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Scene/Component.hpp>
#include <Scene/Scene.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
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

class ComponentLoadContext final : public SerializationContext
{
public:
	explicit ComponentLoadContext(ResourceSession& resourceSession)
	    : SerializationContext(&resourceSession), resources(resourceSession)
	{
	}

	ResourceSession& Resources() const
	{
		return resources;
	}

	Result<GameObject::Ptr> GameObjectById(ResourceId id) const;

	Result<ComponentId> Component(ResourceId id) const;

	Result<void> MapGameObject(ResourceId id, GameObject::Ptr object);

	Result<void> MapComponent(ResourceId id, ComponentId component);

	Result<::Ursa::Component*> ComponentObjectById(ResourceId id) const;

	Result<void> MapComponentObject(ResourceId id, ::Ursa::Component* component);

	void SetSceneReadyCallback(std::function<Result<void>(Scene&, ComponentLoadContext&)> callback)
	{
		sceneReadyCallback = std::move(callback);
	}

	Result<void> NotifySceneReady(Scene& scene)
	{
		return sceneReadyCallback ? sceneReadyCallback(scene, *this) : Result<void>{};
	}

	void ClearMappings();

private:
	ResourceSession& resources;

	Map<String, GameObject::Ptr> gameObjects;
	Map<String, ComponentId> components;
	Map<String, ::Ursa::Component*> componentObjects;
	std::function<Result<void>(Scene&, ComponentLoadContext&)> sceneReadyCallback;
};

// Clear candidate reference maps on failure so callers cannot observe a partial graph.
class ComponentMappingScope final
{
public:
	explicit ComponentMappingScope(ComponentLoadContext& context) : context(context)
	{
		context.ClearMappings();
	}

	~ComponentMappingScope()
	{
		if (!committed)
		{
			context.ClearMappings();
		}
	}

	void Commit()
	{
		committed = true;
	}

private:
	ComponentLoadContext& context;
	bool committed = false;
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

	template <class T, class F> Result<void> RegisterSerializable(String type, UInt32 version, F factory)
	{
		ComponentCodec codec;
		codec.type = std::move(type);
		codec.version = version;
		codec.matches = [](const Component& value) { return dynamic_cast<const T*>(&value) != nullptr; };
		codec.save = [](const Component& component, const ComponentSaveContext& context)
		{
			SerializationContext saveContext;
			saveContext.SetResourceIdResolver(
			    [&context](const Serializable& value) -> Result<ResourceId>
			    {
				    auto* material = dynamic_cast<const MaterialInstance*>(&value);
				    if (!material || !context.materialId)
				    {
					    return Result<ResourceId>::Failure("No material ID resolver for BoxRenderComponent");
				    }
				    return context.materialId(std::shared_ptr<MaterialInstance>(const_cast<MaterialInstance*>(material),
				                                                                [](MaterialInstance*) {}));
			    });
			ArchiveWriter writer;
			writer.SetContext(&saveContext);
			auto saved = component.Serialize(writer);
			return saved ? ArchiveToJson(writer) : Result<nlohmann::json>::Failure(saved.error);
		};
		codec.createDetached = [factory](const GameObject::Ptr& owner, const nlohmann::json& data,
		                                 ComponentLoadContext& context) -> Result<Component*>
		{
			auto reader = ArchiveFromJson(data);
			if (!reader)
			{
				return Result<Component*>::Failure(reader.error);
			}
			auto created = factory(owner, context);
			if (!created)
			{
				return Result<Component*>::Failure(created.error);
			}
			auto loaded = created.value->Deserialize(reader.value, context);
			return loaded ? Result<Component*>::Success(created.value) : Result<Component*>::Failure(loaded.error);
		};
		codec.create = [create = codec.createDetached](Scene&, const GameObject::Ptr& owner, const nlohmann::json& data,
		                                               ComponentLoadContext& context)
		{
			auto result = create(owner, data, context);
			return result ? Result<ComponentId>::Success(result.value->Id())
			              : Result<ComponentId>::Failure(result.error);
		};
		return RegisterCodec(std::move(codec));
	}

	Result<const ComponentCodec*> FindFor(const Component& component) const;

	const ComponentCodec* Find(const String& type) const;

	Result<void> RegisterScriptFieldAlias(const String& scriptType, const String& formerName,
	                                      const String& currentName);

	Result<void> ReplaceScriptFieldAliases(const String& scriptType, Map<String, String> aliases);

	Map<String, String> ScriptFieldAliases(const String& scriptType) const;

private:
	Map<String, ComponentCodec> codecs;
	Map<String, Map<String, String>> scriptFieldAliases;
};

Result<void> RegisterBuiltinComponents(ComponentRegistry& registry);
} // namespace Ursa
