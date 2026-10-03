#include <Core/StringInterop.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <utility>

namespace Ursa
{
Result<NodeId> ComponentLoadContext::Node(ResourceId id) const
{
	auto found = nodes.find(id.ToString());
	return found == nodes.end() ? Result<NodeId>::Failure("Missing persistent node ID " + id.ToString())
	                            : Result<NodeId>::Success(found->second);
}

Result<ComponentId> ComponentLoadContext::Component(ResourceId id) const
{
	auto found = components.find(id.ToString());
	return found == components.end() ? Result<ComponentId>::Failure("Missing persistent component ID " + id.ToString())
	                                 : Result<ComponentId>::Success(found->second);
}

Result<void> ComponentLoadContext::MapNode(ResourceId id, NodeId node)
{
	if (!id || !node || !nodes.emplace(id.ToString(), node).second)
	{
		return Result<void>::Failure("Duplicate or invalid persistent node ID " + id.ToString());
	}
	return {};
}

Result<void> ComponentLoadContext::MapComponent(ResourceId id, ComponentId component)
{
	if (!id || !component || !components.emplace(id.ToString(), component).second)
	{
		return Result<void>::Failure("Duplicate or invalid persistent component ID " + id.ToString());
	}
	return {};
}

void ComponentLoadContext::ClearMappings()
{
	nodes.clear();
	components.clear();
}

Result<void> ComponentRegistry::RegisterCodec(ComponentCodec codec)
{
	if (codec.type.empty() || !codec.version || !codec.matches || !codec.save || !codec.create)
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
} // namespace Ursa
