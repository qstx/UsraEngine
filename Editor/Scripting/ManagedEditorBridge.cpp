#include <Scripting/ManagedEditorBridge.hpp>

#if defined(URSA_ENABLE_DOTNET)
#include <Serialization/SerializedObject.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <cstring>
#include <thread>
#include <stdexcept>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

struct Invocation
{
	ManagedEditorContext& source;
	std::unique_ptr<SerializedObject> properties;
	String cachedRequest;
	String cachedResponse;
	std::thread::id thread = std::this_thread::get_id();
	Int32 groups = 0;
	bool changed = false;
};

Json GetProperty(Invocation& call, const String& path)
{
	if (!call.properties)
	{
		throw std::runtime_error("This extension has no serialized component target");
	}
	auto property = call.properties->FindProperty(path);
	if (!property)
	{
		throw std::runtime_error(ToStdString(property.error));
	}
	auto value = property.value.Value();
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return value.value;
}

void SetProperty(Invocation& call, const String& path, Json value)
{
	if (!call.properties)
	{
		throw std::runtime_error("This extension has no serialized component target");
	}
	auto property = call.properties->FindProperty(path);
	if (!property)
	{
		throw std::runtime_error(ToStdString(property.error));
	}
	auto saved = property.value.SetValue(std::move(value));
	if (!saved)
	{
		throw std::runtime_error(ToStdString(saved.error));
	}
}

bool Apply(Invocation& call)
{
	if (!call.properties)
	{
		throw std::runtime_error("This extension has no serialized component target");
	}
	const bool modified = call.properties->HasModifiedProperties();
	auto result = call.properties->Apply();
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	if (modified)
	{
		call.changed = true;
		if (call.source.document)
		{
			call.source.document->MarkDirty();
		}
	}
	return modified;
}

Json ChooseObject(Invocation& call, const char* label, Json value, const String& kind)
{
	if (ImGui::BeginCombo(label, value.is_null() ? "None" : "Assigned"))
	{
		if (ImGui::Selectable("None", value.is_null()))
		{
			value = nullptr;
		}
		Vector<GameObject::Ptr> objects = call.source.selection.roots;
		if (objects.empty() && call.source.selection.scene)
		{
			objects = call.source.selection.scene->RootGameObjects();
		}
		while (!objects.empty())
		{
			auto object = std::move(objects.back());
			objects.pop_back();
			for (const auto& child : object->Children())
			{
				objects.push_back(child);
			}
			const String name = object->Name().value;
			if (kind == "GameObject")
			{
				const String id = object->Id().ToString();
				ImGui::PushID(id.c_str());
				if (ImGui::Selectable(name.c_str()))
				{
					value = {{"$ursaReference", {{"Kind", "GameObject"}, {"Id", ToStdString(id)}}}};
				}
				ImGui::PopID();
			}
			else
			{
				for (Component* component : object->Components())
				{
					const String id = component->PersistentId().ToString();
					const String type = dynamic_cast<Scripting::ScriptBehaviourComponent*>(component)
					                        ? static_cast<Scripting::ScriptBehaviourComponent*>(component)->TypeName()
					                        : component->SerializationType();
					ImGui::PushID(id.c_str());
					if (ImGui::Selectable((name + " / " + type).c_str()))
					{
						value = {{"$ursaReference", {{"Kind", "Component"}, {"Id", ToStdString(id)}}}};
					}
					ImGui::PopID();
				}
			}
		}
		ImGui::EndCombo();
	}
	return value;
}

Json ChooseAsset(Invocation& call, const char* label, Json value)
{
	if (ImGui::BeginCombo(label, value.is_null() ? "None" : "Assigned"))
	{
		if (ImGui::Selectable("None", value.is_null()))
		{
			value = nullptr;
		}
		if (call.source.workspace)
		{
			for (const auto& entry : call.source.workspace->Resources())
			{
				if (ImGui::Selectable(entry.location.path.ToString().c_str()))
				{
					value = {{"guid", ToStdString(entry.id.ToString())}, {"localFileId", UInt64{0}}};
				}
			}
		}
		ImGui::EndCombo();
	}
	return value;
}

Json DrawValue(Invocation& call, const char* label, Json value, const Json& descriptor = Json::object())
{
	const std::string kind = descriptor.value("Kind", std::string{});
	if (kind == "GameObjectReference" || kind == "ComponentReference")
	{
		return ChooseObject(call, label, std::move(value), kind == "GameObjectReference" ? "GameObject" : "Component");
	}
	if (kind == "AssetReference")
	{
		return ChooseAsset(call, label, std::move(value));
	}
	if (kind == "Enumeration" && descriptor.contains("EnumNumericValues"))
	{
		const auto& names = descriptor["EnumValues"];
		const auto& numbers = descriptor["EnumNumericValues"];
		std::string preview = value.dump();
		for (Size index = 0; index < names.size() && index < numbers.size(); ++index)
		{
			if (numbers[index] == value)
			{
				preview = names[index].get<std::string>();
			}
		}
		if (ImGui::BeginCombo(label, preview.c_str()))
		{
			for (Size index = 0; index < names.size() && index < numbers.size(); ++index)
			{
				if (ImGui::Selectable(names[index].get<std::string>().c_str(), numbers[index] == value))
				{
					value = numbers[index];
				}
			}
			ImGui::EndCombo();
		}
		return value;
	}
	if (value.is_boolean())
	{
		bool edited = value.get<bool>();
		ImGui::Checkbox(label, &edited);
		return edited;
	}
	if (value.is_number_float() || kind == "FloatingPoint")
	{
		Float64 edited = value.get<Float64>();
		ImGui::InputDouble(label, &edited);
		return edited;
	}
	if (value.is_number_integer())
	{
		if (value.is_number_unsigned() && kind != "SignedInteger")
		{
			UInt64 edited = value.get<UInt64>();
			ImGui::InputScalar(label, ImGuiDataType_U64, &edited);
			return edited;
		}
		Int64 edited = value.get<Int64>();
		ImGui::InputScalar(label, ImGuiDataType_S64, &edited);
		return edited;
	}
	if (value.is_string() || (value.is_null() && kind == "String"))
	{
		const std::string text = value.is_string() ? value.get<std::string>() : std::string{};
		Vector<char> buffer(text.size() + 4096, 0);
		std::memcpy(buffer.data(), text.data(), text.size());
		return ImGui::InputText(label, buffer.data(), buffer.size()) ? Json(std::string(buffer.data())) : value;
	}
	if (kind == "Vector3" || kind == "Quaternion" || kind == "Color")
	{
		Float32 components[4]{};
		const Size count = kind == "Vector3" ? 3 : 4;
		for (Size index = 0; index < count; ++index)
		{
			components[index] = value.at(index).get<Float32>();
		}
		const bool changed = kind == "Color" ? ImGui::ColorEdit4(label, components)
		                     : count == 3    ? ImGui::DragFloat3(label, components, 0.05f)
		                                     : ImGui::DragFloat4(label, components, 0.05f);
		if (changed)
		{
			value = Json::array();
			for (Size index = 0; index < count; ++index)
			{
				value.push_back(components[index]);
			}
		}
		return value;
	}
	if (value.is_array())
	{
		ImGui::TextUnformatted(label);
		const std::string elementKind = descriptor.value("ElementKind", std::string{});
		Json elementDescriptor = descriptor;
		elementDescriptor["Kind"] = elementKind;
		for (Size index = 0; index < value.size(); ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			value[index] = DrawValue(call, "Element", value[index], elementDescriptor);
			ImGui::SameLine();
			if (ImGui::SmallButton("-"))
			{
				value.erase(value.begin() + static_cast<std::ptrdiff_t>(index));
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (ImGui::SmallButton("Add element"))
		{
			Json element = nullptr;
			if (elementKind == "Boolean")
			{
				element = false;
			}
			else if (elementKind == "SignedInteger")
			{
				element = Int64{0};
			}
			else if (elementKind == "UnsignedInteger")
			{
				element = UInt64{0};
			}
			else if (elementKind == "FloatingPoint")
			{
				element = 0.0;
			}
			else if (elementKind == "String")
			{
				element = "";
			}
			else if (elementKind == "Vector3")
			{
				element = {0.0, 0.0, 0.0};
			}
			else if (elementKind == "Quaternion" || elementKind == "Color")
			{
				element = {0.0, 0.0, 0.0, 0.0};
			}
			else if (elementKind == "Enumeration" && descriptor.contains("EnumNumericValues") &&
			         !descriptor["EnumNumericValues"].empty())
			{
				element = descriptor["EnumNumericValues"][0];
			}
			value.push_back(element);
		}
		return value;
	}
	ImGui::Text("%s: unsupported field", label);
	return value;
}

Json PropertyDescriptor(Invocation& call, const String& path)
{
	auto* script = dynamic_cast<Scripting::ScriptBehaviourComponent*>(call.source.target);
	if (!script)
	{
		return Json::object();
	}
	const Json payload = Json::parse(ToStdString(script->SerializedPayload()));
	const std::string field = ToStdString(path.substr(String("/Fields/").size()));
	return payload.contains("FieldTypes") && payload["FieldTypes"].contains(field) ? payload["FieldTypes"][field]
	                                                                               : Json::object();
}

Json Dispatch(Invocation& call, const Json& request)
{
	const std::string operation = request.at("Operation").get<std::string>();
	const Json parameters = request.value("Parameters", Json::object());
	const auto parameter = [&](const char* key) { return ToUrsaString(parameters.at(key).get<std::string>()); };
	if (operation == "ReadProperty")
	{
		return GetProperty(call, parameter("Path"));
	}
	if (operation == "WriteProperty")
	{
		SetProperty(call, parameter("Path"), parameters.at("Value"));
		return true;
	}
	if (operation == "ApplyProperties")
	{
		return Apply(call);
	}
	if (operation == "Context")
	{
		Json result{{"Selection", Json::array()}, {"Document", nullptr}};
		if (call.source.selection.object)
		{
			auto object = call.source.selection.object;
			result["Selection"].push_back(
			    {{"Id", ToStdString(object->Id().ToString())}, {"Name", ToStdString(object->Name().value)}});
		}
		if (call.source.document)
		{
			result["Document"] = {{"Id", ToStdString(call.source.document->Id().ToString())},
			                      {"Path", ToStdString(call.source.document->Path().ToString())},
			                      {"Kind", call.source.document->Kind() == ResourceKind::Prefab ? "Prefab" : "Scene"},
			                      {"Dirty", call.source.document->Dirty()}};
		}
		return result;
	}
	if (operation == "Assets")
	{
		Json assets = Json::array();
		if (call.source.workspace)
		{
			for (const auto& entry : call.source.workspace->Resources())
			{
				assets.push_back({{"Guid", ToStdString(entry.id.ToString())},
				                  {"Path", ToStdString(entry.location.path.ToString())}});
			}
		}
		return assets;
	}
	if (operation == "MarkDirty")
	{
		if (!call.source.document)
		{
			throw std::runtime_error("No editing document is open");
		}
		call.source.document->MarkDirty();
		return true;
	}
	if (operation == "Save")
	{
		if (!call.source.documents)
		{
			throw std::runtime_error("No document service is available");
		}
		auto saved = call.source.documents->SaveCurrent();
		if (!saved)
		{
			throw std::runtime_error(ToStdString(saved.error));
		}
		return true;
	}
	if (operation == "OpenAsset")
	{
		auto path = VirtualPath::Parse(parameter("Path"));
		if (!path || !call.source.openAsset)
		{
			throw std::runtime_error("Asset opening is unavailable or the VFS path is invalid");
		}
		auto opened = call.source.openAsset(path.value);
		if (!opened)
		{
			throw std::runtime_error(ToStdString(opened.error));
		}
		return true;
	}
	if (!ImGui::GetCurrentContext())
	{
		throw std::runtime_error("GUI controls require an active Editor frame");
	}
	if (operation == "Text")
	{
		ImGui::TextUnformatted(parameter("Text").c_str());
		return true;
	}
	if (operation == "Button")
	{
		return ImGui::Button(parameter("Label").c_str());
	}
	if (operation == "SameLine")
	{
		ImGui::SameLine();
		return true;
	}
	if (operation == "Separator")
	{
		ImGui::Separator();
		return true;
	}
	if (operation == "BeginGroup")
	{
		ImGui::BeginGroup();
		++call.groups;
		return true;
	}
	if (operation == "EndGroup")
	{
		if (call.groups == 0)
		{
			throw std::runtime_error("Editor layout scope is unbalanced");
		}
		ImGui::EndGroup();
		--call.groups;
		return true;
	}
	if (operation == "Popup")
	{
		Int32 selected = parameters.at("Value").get<Int32>();
		const Json& options = parameters.at("Options");
		const std::string preview = selected >= 0 && selected < static_cast<Int32>(options.size())
		                                ? options[selected].get<std::string>()
		                                : std::string{};
		if (ImGui::BeginCombo(parameter("Label").c_str(), preview.c_str()))
		{
			for (Size index = 0; index < options.size(); ++index)
			{
				if (ImGui::Selectable(options[index].get<std::string>().c_str(), selected == static_cast<Int32>(index)))
				{
					selected = static_cast<Int32>(index);
				}
			}
			ImGui::EndCombo();
		}
		return selected;
	}
	if (operation == "PropertyField")
	{
		const String path = parameter("Path");
		Json before = GetProperty(call, path);
		Json after = DrawValue(call, parameter("Label").c_str(), before, PropertyDescriptor(call, path));
		if (before != after)
		{
			SetProperty(call, path, std::move(after));
		}
		return true;
	}
	if (operation == "DrawDefaultInspector")
	{
		if (!call.properties || !call.source.target)
		{
			throw std::runtime_error("No Inspector target is selected");
		}
		Json fields;
		if (auto* script = dynamic_cast<Scripting::ScriptBehaviourComponent*>(call.source.target))
		{
			fields = Json::parse(ToStdString(script->SerializedPayload())).at("Fields");
		}
		else
		{
			auto codec = call.source.registry->FindFor(*call.source.target);
			auto saved = codec.value->save(*call.source.target, call.source.saveContext);
			if (!saved)
			{
				throw std::runtime_error(ToStdString(saved.error));
			}
			fields = std::move(saved.value);
		}
		for (const auto& field : fields.items())
		{
			const String path = String("/Fields/") + ToUrsaString(field.key());
			const Json descriptor = PropertyDescriptor(call, path);
			if (dynamic_cast<Scripting::ScriptBehaviourComponent*>(call.source.target) && descriptor.empty())
			{
				ImGui::TextDisabled("%s: preserved unknown field", field.key().c_str());
				continue;
			}
			Json before = GetProperty(call, path);
			Json after = DrawValue(call, field.key().c_str(), before, descriptor);
			if (before != after)
			{
				SetProperty(call, path, std::move(after));
			}
		}
		return Apply(call);
	}
	Json descriptor = Json::object();
	if (operation == "FloatField")
	{
		descriptor["Kind"] = "FloatingPoint";
	}
	else if (operation == "ColorField")
	{
		descriptor["Kind"] = "Color";
	}
	else if (operation == "VectorField")
	{
		descriptor["Kind"] = parameters.at("Value").size() == 3 ? "Vector3" : "Quaternion";
	}
	else if (operation == "ObjectField")
	{
		return ChooseObject(call, parameter("Label").c_str(), parameters.at("Value"), parameter("Kind"));
	}
	else if (operation == "AssetField")
	{
		return ChooseAsset(call, parameter("Label").c_str(), parameters.at("Value"));
	}
	else if (operation != "Toggle" && operation != "IntField" && operation != "FloatField" && operation != "TextField")
	{
		throw std::runtime_error("Unsupported Editor operation");
	}
	return DrawValue(call, parameter("Label").c_str(), parameters.at("Value"), descriptor);
}

Int32 Exchange(void* context, const UInt8* request, UInt64 length, UInt8* output, UInt64 capacity, UInt64* required)
{
	auto* call = static_cast<Invocation*>(context);
	if (!call || !request || length == 0 || length > 16ull * 1024 * 1024 || !required || (!output && capacity != 0) ||
	    call->thread != std::this_thread::get_id())
	{
		return 1;
	}
	const String text(reinterpret_cast<const char*>(request), static_cast<Size>(length));
	if (capacity == 0)
	{
		call->cachedRequest = text;
		try
		{
			call->cachedResponse = ToUrsaString(Dispatch(*call, Json::parse(ToStdString(text))).dump());
		}
		catch (const std::exception& error)
		{
			call->cachedResponse = ToUrsaString(Json{{"Error", error.what()}}.dump());
		}
	}
	else if (call->cachedRequest != text)
	{
		return 2;
	}
	*required = call->cachedResponse.size();
	if (capacity < call->cachedResponse.size())
	{
		return 4;
	}
	std::memcpy(output, call->cachedResponse.data(), call->cachedResponse.size());
	call->cachedRequest.clear();
	call->cachedResponse.clear();
	return 0;
}
} // namespace

Result<bool> InvokeManagedEditorExtension(Scripting::DotNetHost& host, UInt64 sessionId, const String& extensionId,
                                          ManagedEditorContext& context)
{
	Invocation call{context};
	if (context.target)
	{
		if (!context.registry || !context.selection.loadContext)
		{
			return Result<bool>::Failure("Inspector context has no component or resource services");
		}
		auto properties = SerializedObject::Create(*context.target, *context.registry, *context.selection.loadContext,
		                                           context.saveContext);
		if (!properties)
		{
			return Result<bool>::Failure(properties.error);
		}
		call.properties = std::move(properties.value);
	}
	Scripting::ManagedEditorApi api;
	api.size = sizeof(api);
	api.context = &call;
	api.exchange = &Exchange;
	auto invoked = host.InvokeEditorExtension(sessionId, extensionId, api);
	while (call.groups > 0)
	{
		ImGui::EndGroup();
		--call.groups;
	}
	if (!invoked || !invoked.value)
	{
		return Result<bool>::Failure(invoked ? "Managed editor extension was disabled after an exception"
		                                     : invoked.error);
	}
	return Result<bool>::Success(call.changed);
}
} // namespace Ursa::Editor
#endif
