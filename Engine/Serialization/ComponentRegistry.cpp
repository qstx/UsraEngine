#include <Core/StringInterop.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <utility>

namespace Ursa
{
Result<GameObject::Ptr> ComponentLoadContext::GameObjectById(ResourceId id) const
{
	auto found = gameObjects.find(id.ToString());
	return found == gameObjects.end()
	           ? Result<GameObject::Ptr>::Failure("Missing persistent GameObject ID " + id.ToString())
	           : Result<GameObject::Ptr>::Success(found->second);
}

Result<ComponentId> ComponentLoadContext::Component(ResourceId id) const
{
	auto found = components.find(id.ToString());
	return found == components.end() ? Result<ComponentId>::Failure("Missing persistent component ID " + id.ToString())
	                                 : Result<ComponentId>::Success(found->second);
}

Result<void> ComponentLoadContext::MapGameObject(ResourceId id, GameObject::Ptr object)
{
	if (!id || !object || !gameObjects.emplace(id.ToString(), object).second)
	{
		return Result<void>::Failure("Duplicate or invalid persistent GameObject ID " + id.ToString());
	}
	return RegisterObject(id, *object);
}

Result<void> ComponentLoadContext::MapComponent(ResourceId id, ComponentId component)
{
	if (!id || !component || !components.emplace(id.ToString(), component).second)
	{
		return Result<void>::Failure("Duplicate or invalid persistent component ID " + id.ToString());
	}
	return {};
}

Result<::Ursa::Component*> ComponentLoadContext::ComponentObjectById(ResourceId id) const
{
	auto found = componentObjects.find(id.ToString());
	return found == componentObjects.end()
	           ? Result<::Ursa::Component*>::Failure("Missing persistent component ID " + id.ToString())
	           : Result<::Ursa::Component*>::Success(found->second);
}

Result<void> ComponentLoadContext::MapComponentObject(ResourceId id, ::Ursa::Component* component)
{
	if (!id || !component || !componentObjects.emplace(id.ToString(), component).second)
	{
		return Result<void>::Failure("Duplicate or invalid persistent component ID " + id.ToString());
	}
	return RegisterObject(id, *component);
}

void ComponentLoadContext::ClearMappings()
{
	gameObjects.clear();
	components.clear();
	componentObjects.clear();
	ClearReferences();
}

Result<void> ComponentRegistry::RegisterCodec(ComponentCodec codec)
{
	if (codec.type.empty() || !codec.version || !codec.matches || !codec.save ||
	    (!codec.create && !codec.createDetached))
	{
		return Result<void>::Failure("Component codec needs a type, version, matcher, saver and factory");
	}
	const auto name = codec.type;
	if (!codecs.emplace(name, std::move(codec)).second)
	{
		return Result<void>::Failure("Duplicate component codec type " + name);
	}
	return {};
}

Result<const ComponentCodec*> ComponentRegistry::FindFor(const Component& component) const
{
	const ComponentCodec* selected = nullptr;
	for (const auto& entry : codecs)
	{
		if (entry.second.matches(component))
		{
			if (selected)
			{
				return Result<const ComponentCodec*>::Failure("Multiple component codecs match one component");
			}
			selected = &entry.second;
		}
	}
	if (!selected)
	{
		return Result<const ComponentCodec*>::Failure("No registered codec for component type " +
		                                              String(typeid(component).name()));
	}
	return Result<const ComponentCodec*>::Success(selected);
}

const ComponentCodec* ComponentRegistry::Find(const String& type) const
{
	const auto found = codecs.find(type);
	return found == codecs.end() ? nullptr : &found->second;
}

Result<void> ComponentRegistry::RegisterScriptFieldAlias(const String& scriptType, const String& formerName,
                                                         const String& currentName)
{
	if (scriptType.empty() || formerName.empty() || currentName.empty() || formerName == currentName)
	{
		return Result<void>::Failure("Script field alias needs a type, former name, and current name");
	}
	auto& aliases = scriptFieldAliases[scriptType];
	auto found = aliases.find(formerName);
	if (found != aliases.end() && found->second != currentName)
	{
		return Result<void>::Failure("Conflicting FormerlySerializedAs mapping for " + scriptType + "." + formerName);
	}
	aliases[formerName] = currentName;
	return {};
}

Result<void> ComponentRegistry::ReplaceScriptFieldAliases(const String& scriptType, Map<String, String> aliases)
{
	if (scriptType.empty())
	{
		return Result<void>::Failure("Script identity is empty");
	}
	for (const auto& alias : aliases)
	{
		if (alias.first.empty() || alias.second.empty() || alias.first == alias.second)
		{
			return Result<void>::Failure("Script field alias has an invalid old/current name");
		}
	}
	if (aliases.empty())
	{
		scriptFieldAliases.erase(scriptType);
	}
	else
	{
		scriptFieldAliases[scriptType] = std::move(aliases);
	}
	return {};
}

Map<String, String> ComponentRegistry::ScriptFieldAliases(const String& scriptType) const
{
	auto found = scriptFieldAliases.find(scriptType);
	return found == scriptFieldAliases.end() ? Map<String, String>{} : found->second;
}
} // namespace Ursa
