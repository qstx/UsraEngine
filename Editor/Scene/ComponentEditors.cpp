#include <Scene/ComponentEditors.hpp>
#include <Scene/Lighting.hpp>
#include <Scene/RenderingComponents.hpp>
#include <algorithm>

namespace Ursa::Editor
{
Result<void> ComponentEditors::RegisterEditor(String type, ComponentEditor editor)
{
	if (type.empty() || !editor)
	{
		return Result<void>::Failure("Component editor requires a type and callback");
	}
	if (!editors.emplace(std::move(type), std::move(editor)).second)
	{
		return Result<void>::Failure("Component editor already registered");
	}
	return {};
}

const ComponentEditor* ComponentEditors::Find(const String& type) const
{
	auto found = editors.find(type);
	return found == editors.end() ? nullptr : &found->second;
}

Result<void> ComponentEditors::RegisterType(ComponentTypeDescriptor descriptor, ComponentEditor editor)
{
	if (descriptor.type.empty() || descriptor.displayName.empty() || descriptor.category.empty() ||
	    !descriptor.version || !descriptor.create)
	{
		return Result<void>::Failure("Component type requires a stable ID, display name, category, and factory");
	}
	if (types.find(descriptor.type) != types.end() || (editor && editors.find(descriptor.type) != editors.end()))
	{
		return Result<void>::Failure("Component editor type is already registered: " + descriptor.type);
	}
	const String type = descriptor.type;
	types.emplace(type, std::move(descriptor));
	if (editor)
	{
		editors.emplace(type, std::move(editor));
	}
	return {};
}

const ComponentTypeDescriptor* ComponentEditors::FindType(const String& type) const
{
	auto found = types.find(type);
	return found == types.end() ? nullptr : &found->second;
}

Vector<ComponentTypeDescriptor> ComponentEditors::Types() const
{
	Vector<ComponentTypeDescriptor> result;
	result.reserve(types.size());
	for (const auto& item : types)
	{
		result.push_back(item.second);
	}
	return result;
}

Result<void> ComponentEditors::CanCreate(const String& type, const ComponentInspectorContext& context) const
{
	const auto* descriptor = FindType(type);
	if (!descriptor)
	{
		return Result<void>::Failure("Unregistered component type " + type);
	}
	if (!context.object)
	{
		return Result<void>::Failure("Select a GameObject before adding a component");
	}
	if (!descriptor->allowMultiple)
	{
		for (Component* component : context.object->Components())
		{
			if (component->SerializationType() == type)
			{
				return Result<void>::Failure("This GameObject already has " + descriptor->displayName);
			}
		}
	}
	return descriptor->available ? descriptor->available(context) : Result<void>{};
}

Result<Component*> ComponentEditors::Create(const String& type, ComponentInspectorContext& context) const
{
	const auto* descriptor = FindType(type);
	if (!descriptor)
	{
		return Result<Component*>::Failure("Unregistered component type " + type);
	}
	auto available = CanCreate(type, context);
	if (!available)
	{
		return Result<Component*>::Failure(available.error);
	}
	auto previous = context.object->Components();
	auto created = descriptor->create(context);
	if (!created)
	{
		for (Component* component : context.object->Components())
		{
			if (std::find(previous.begin(), previous.end(), component) == previous.end())
			{
				context.object->RemoveComponent(component);
			}
		}
		return created;
	}
	if (!created.value || created.value->SerializationType() != type ||
	    created.value->GetGameObject() != context.object)
	{
		if (created.value && created.value->GetGameObject() == context.object)
		{
			context.object->RemoveComponent(created.value);
		}
		return Result<Component*>::Failure("Component factory returned an invalid object for " + type);
	}
	return created;
}

namespace
{
template <class T> Result<Component*> CreateBuiltinComponent(ComponentInspectorContext& context)
{
	auto added = context.object->AddComponent<T>();
	return added ? Result<Component*>::Success(added.value) : Result<Component*>::Failure(added.error);
}
} // namespace

Result<void> RegisterBuiltinComponentEditors(ComponentEditors& editors)
{
	auto result =
	    editors.RegisterType({"ursa.Camera", "Camera", "Rendering", CreateBuiltinComponent<CameraComponent>, {}});
	if (result)
	{
		result = editors.RegisterType(
		    {"ursa.BoxRender", "Box Renderer", "Rendering", CreateBuiltinComponent<BoxRenderComponent>, {}});
	}
	if (result)
	{
		result = editors.RegisterType({"ursa.DirectionalLight",
		                               "Directional Light",
		                               "Lighting",
		                               CreateBuiltinComponent<DirectionalLightComponent>,
		                               {}});
	}
	if (result)
	{
		result = editors.RegisterType(
		    {"ursa.PointLight", "Point Light", "Lighting", CreateBuiltinComponent<PointLightComponent>, {}});
	}
	return result;
}
} // namespace Ursa::Editor
