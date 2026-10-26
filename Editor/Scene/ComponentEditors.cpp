#include <Scene/ComponentEditors.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <algorithm>
#include <Scene/Lighting.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Core/StringInterop.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Ursa::Editor
{
namespace
{
String StableComponentType(const Component& component)
{
	if (const auto* script = dynamic_cast<const Scripting::ScriptBehaviourComponent*>(&component))
	{
		return "script:" + script->TypeName();
	}
	return "native:" + component.SerializationType();
}
} // namespace

Result<Vector<ComponentTargetGroup>> BuildCommonComponentMapping(const Vector<GameObject::Ptr>& targets,
                                                                 Size primaryIndex)
{
	if (targets.empty() || primaryIndex >= targets.size())
	{
		return Result<Vector<ComponentTargetGroup>>::Failure("Component mapping requires a valid primary target");
	}
	for (Size index = 0; index < targets.size(); ++index)
	{
		if (!targets[index])
		{
			return Result<Vector<ComponentTargetGroup>>::Failure("Component mapping contains an empty target");
		}
		if (std::find(targets.begin(), targets.begin() + index, targets[index]) != targets.begin() + index)
		{
			return Result<Vector<ComponentTargetGroup>>::Failure("Component mapping contains a duplicate target");
		}
	}

	Vector<Map<String, Vector<Component*>>> componentsByTarget;
	componentsByTarget.reserve(targets.size());
	for (const auto& target : targets)
	{
		Map<String, Vector<Component*>> byType;
		for (Component* component : target->Components())
		{
			byType[StableComponentType(*component)].push_back(component);
		}
		componentsByTarget.push_back(std::move(byType));
	}

	Vector<ComponentTargetGroup> common;
	Map<String, Size> occurrenceByType;
	for (Component* primaryComponent : targets[primaryIndex]->Components())
	{
		const String type = StableComponentType(*primaryComponent);
		const Size ordinal = occurrenceByType[type]++;
		ComponentTargetGroup group{type, ordinal, {}};
		group.targets.reserve(targets.size());
		bool presentEverywhere = true;
		for (const auto& byType : componentsByTarget)
		{
			auto found = byType.find(type);
			if (found == byType.end() || ordinal >= found->second.size())
			{
				presentEverywhere = false;
				break;
			}
			group.targets.push_back(found->second[ordinal]);
		}
		if (presentEverywhere)
		{
			common.push_back(std::move(group));
		}
	}
	return Result<Vector<ComponentTargetGroup>>::Success(std::move(common));
}

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

const ComponentHeaderEditor* ComponentEditors::FindHeader(const String& type) const
{
	auto found = headerEditors.find(type);
	return found == headerEditors.end() ? nullptr : &found->second;
}

bool ComponentEditors::SupportsMultipleObjects(const String& type) const
{
	const auto found = multipleObjectEditors.find(type);
	return found != multipleObjectEditors.end() && found->second;
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

void ComponentEditors::ReplaceEditor(const String& type, ComponentEditor editor, bool supportsMultipleObjects)
{
	if (editor)
	{
		editors[type] = std::move(editor);
		multipleObjectEditors[type] = supportsMultipleObjects;
	}
	else
	{
		editors.erase(type);
		headerEditors.erase(type);
		multipleObjectEditors.erase(type);
	}
}

void ComponentEditors::ReplaceHeaderEditor(const String& type, ComponentHeaderEditor editor)
{
	if (editor)
	{
		headerEditors[type] = std::move(editor);
	}
	else
	{
		headerEditors.erase(type);
	}
}

void ComponentEditors::UnregisterType(const String& type)
{
	types.erase(type);
	editors.erase(type);
	headerEditors.erase(type);
	multipleObjectEditors.erase(type);
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
			const auto* script = dynamic_cast<const Scripting::ScriptBehaviourComponent*>(component);
			if ((script && script->TypeName() == type) || component->SerializationType() == type)
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
	const auto* script =
	    created.value ? dynamic_cast<Ursa::Scripting::ScriptBehaviourComponent*>(created.value) : nullptr;
	const bool typeMatches =
	    created.value && (script ? script->TypeName() == type : created.value->SerializationType() == type);
	if (!created.value || !typeMatches || created.value->GetGameObject() != context.object)
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
		    {"ursa.MeshFilter", "Mesh Filter", "Rendering", CreateBuiltinComponent<MeshFilterComponent>, {}, false, 1});
	}
	const ComponentAvailability rendererAvailable = [](const ComponentInspectorContext& context) -> Result<void>
	{
		for (const auto* component : context.object->Components())
		{
			if (dynamic_cast<const RendererComponent*>(component))
			{
				return Result<void>::Failure("This GameObject already has a Renderer");
			}
		}
		return {};
	};
	if (result)
	{
		result = editors.RegisterType({"ursa.MeshRenderer", "Mesh Renderer", "Rendering",
		                               CreateBuiltinComponent<MeshRendererComponent>, rendererAvailable, false, 2});
	}
	if (result)
	{
		result =
		    editors.RegisterType({"ursa.SkinnedMeshRenderer", "Skinned Mesh Renderer", "Rendering",
		                          CreateBuiltinComponent<SkinnedMeshRendererComponent>, rendererAvailable, false, 2});
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

Result<void> RegisterScriptComponentType(ComponentEditors& editors, const ScriptTypeDescriptor& scriptType)
{
	if (scriptType.group != ScriptAssemblyGroup::Game || scriptType.kind != ScriptTypeKind::Behaviour ||
	    scriptType.scriptType.empty() || scriptType.clrTypeName.empty() || scriptType.version == 0)
	{
		return Result<void>::Failure("Script component requires a valid Game script type descriptor");
	}
	String displayName = scriptType.clrTypeName.substr(0, scriptType.clrTypeName.find(','));
	const Size namespaceSeparator = displayName.rfind('.');
	if (namespaceSeparator != String::npos)
	{
		displayName = displayName.substr(namespaceSeparator + 1);
	}
	ComponentTypeDescriptor descriptor;
	descriptor.type = scriptType.clrTypeName;
	descriptor.displayName = std::move(displayName);
	descriptor.category = "Scripts";
	descriptor.allowMultiple = true;
	descriptor.version = scriptType.version;
	descriptor.create = [scriptType](ComponentInspectorContext& context) -> Result<Component*>
	{
		auto added = context.object->AddComponent<Scripting::ScriptBehaviourComponent>(scriptType.clrTypeName);
		if (!added)
		{
			return Result<Component*>::Failure(added.error);
		}
		nlohmann::json payload = nlohmann::json::object();
		payload["FormatVersion"] = 1u;
		payload["SourceGuid"] = ToStdString(scriptType.sourceGuid.ToString());
		payload["LocalFileId"] = scriptType.localFileId;
		payload["ScriptType"] = ToStdString(scriptType.scriptType);
		payload["Version"] = scriptType.version;
		payload["Fields"] = nlohmann::json::object();
		payload["FieldTypes"] = nlohmann::json::object();
		payload["FormerFieldNames"] = nlohmann::json::object();
		for (const ScriptFieldTypeDescriptor& field : scriptType.fields)
		{
			try
			{
				payload["Fields"][ToStdString(field.name)] = nlohmann::json::parse(ToStdString(field.defaultValueJson));
			}
			catch (const nlohmann::json::exception& error)
			{
				context.object->RemoveComponent(added.value);
				return Result<Component*>::Failure("Invalid default value for script field " + field.name + ": " +
				                                   ToUrsaString(error.what()));
			}
			nlohmann::json enumNumericValues = nlohmann::json::array();
			for (const String& enumValue : field.enumNumericValuesJson)
			{
				enumNumericValues.push_back(nlohmann::json::parse(ToStdString(enumValue)));
			}
			std::vector<std::string> enumValues;
			for (const String& enumValue : field.enumValues)
			{
				enumValues.push_back(ToStdString(enumValue));
			}
			std::vector<std::string> formerNames;
			for (const String& formerName : field.formerNames)
			{
				formerNames.push_back(ToStdString(formerName));
			}
			payload["FieldTypes"][ToStdString(field.name)] = {
			    {"Kind", ToStdString(field.kind)},
			    {"ElementKind", ToStdString(field.elementKind)},
			    {"TypeName", ToStdString(field.typeName)},
			    {"ElementTypeName", ToStdString(field.elementTypeName)},
			    {"EnumValues", enumValues},
			    {"EnumNumericValues", enumNumericValues},
			    {"EnumUnderlyingType", ToStdString(field.enumUnderlyingType)},
			    {"FormerNames", formerNames}};
			if (!formerNames.empty())
			{
				payload["FormerFieldNames"][ToStdString(field.name)] = formerNames;
			}
		}
		static_cast<Scripting::ScriptBehaviourComponent*>(added.value)
		    ->SetSerializedPayload(ToUrsaString(payload.dump()));
		return Result<Component*>::Success(added.value);
	};
	return editors.RegisterType(std::move(descriptor));
}
} // namespace Ursa::Editor
