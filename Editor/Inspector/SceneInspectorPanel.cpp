#include <Assets/ProjectBrowserModel.hpp>
#include <Ui/EditorIcons.hpp>
#include <Inspector/SceneInspectorPanel.hpp>
#include <Inspector/MeshInspector.hpp>

#include <Core/StringInterop.hpp>
#include <Ui/ImGuiInspectorUI.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/PrefabCapture.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Scene/RenderingComponents.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Ursa::Editor
{
namespace
{
bool DrawAddComponentBrowser(ComponentEditors& componentEditors, ComponentInspectorContext& context, String& diagnostic,
                             const Vector<ScriptTypeDescriptor>* scriptTypes)
{
	if (Icons::Button("Add Component"))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}
	static ImGuiTextFilter filter;
	ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_Appearing);
	bool changed = false;
	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		filter.Draw("Search");
		auto descriptors = componentEditors.Types();
		std::sort(descriptors.begin(), descriptors.end(),
		          [](const ComponentTypeDescriptor& left, const ComponentTypeDescriptor& right)
		          {
			          return left.category == right.category ? left.displayName < right.displayName
			                                                 : left.category < right.category;
		          });
		String currentCategory;
		for (const ComponentTypeDescriptor& descriptor : descriptors)
		{
			if (!filter.PassFilter(descriptor.displayName.c_str()) && !filter.PassFilter(descriptor.category.c_str()))
			{
				continue;
			}
			if (descriptor.category != currentCategory)
			{
				currentCategory = descriptor.category;
				ImGui::SeparatorText(currentCategory.c_str());
			}
			auto available = componentEditors.CanCreate(descriptor.type, context);
			ImGui::BeginDisabled(!available);
			const bool script =
			    scriptTypes &&
			    std::any_of(scriptTypes->begin(), scriptTypes->end(), [&](const ScriptTypeDescriptor& type)
			                { return type.kind == ScriptTypeKind::Behaviour && type.clrTypeName == descriptor.type; });
			if (Icons::Selectable(Icons::ForComponent(descriptor.type, script), descriptor.displayName.c_str()))
			{
				auto created = componentEditors.Create(descriptor.type, context);
				diagnostic = created ? String{} : created.error;
				changed = static_cast<bool>(created);
				if (created)
				{
					ImGui::CloseCurrentPopup();
				}
			}
			if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("%s", available.error.c_str());
			}
			ImGui::EndDisabled();
		}
		ImGui::EndPopup();
	}
	return changed;
}

bool DrawSerializedScriptValue(nlohmann::json& value, const String& kind, const String& elementKind,
                               const nlohmann::json& descriptor, const ProjectWorkspace& workspace,
                               const ComponentInspectorContext& context,
                               const Vector<ScriptTypeDescriptor>* scriptTypes);

bool DrawSerializedReference(nlohmann::json& value, const String& kind, const ComponentInspectorContext& context)
{
	ResourceId currentId;
	if (value.is_object() && value.contains("$ursaReference") && value["$ursaReference"].is_object() &&
	    value["$ursaReference"].contains("Id") && value["$ursaReference"]["Id"].is_string())
	{
		auto parsed = ResourceId::Parse(ToUrsaString(value["$ursaReference"]["Id"].get<std::string>()));
		if (parsed)
		{
			currentId = parsed.value;
		}
	}
	bool changed = false;
	const String preview = currentId ? currentId.ToString() : String("None");
	if (!ImGui::BeginCombo("##Reference", preview.c_str()))
	{
		return false;
	}
	if (Icons::Selectable(Icons::Icon::Close, "None", !currentId))
	{
		value = nullptr;
		changed = true;
	}
	Vector<GameObject::Ptr> pending = context.roots;
	while (!pending.empty())
	{
		GameObject::Ptr object = std::move(pending.back());
		pending.pop_back();
		if (kind == "GameObjectReference")
		{
			const ResourceId id = context.scene ? context.scene->PersistentId(object).value : object->Id();
			const String label = object->Name().value + "##" + id.ToString();
			if (Icons::Selectable(Icons::Icon::Link, label.c_str(), id == currentId))
			{
				value = {{"$ursaReference", {{"Kind", "GameObject"}, {"Id", ToStdString(id.ToString())}}}};
				changed = true;
			}
		}
		else
		{
			for (Component* component : object->Components())
			{
				ResourceId id = component->PersistentId();
				if (context.scene)
				{
					auto persistent = context.scene->ComponentPersistentId(component->Id());
					if (persistent)
					{
						id = persistent.value;
					}
				}
				const String label = component->SerializationType() + "##" + id.ToString();
				if (Icons::Selectable(Icons::Icon::Link, label.c_str(), id == currentId))
				{
					value = {{"$ursaReference", {{"Kind", "Component"}, {"Id", ToStdString(id.ToString())}}}};
					changed = true;
				}
			}
		}
		for (const auto& child : object->Children())
		{
			pending.push_back(child);
		}
	}
	ImGui::EndCombo();
	return changed;
}

bool DrawDirectionalShadowControls(DirectionalLightComponent& light, String& diagnostic)
{
	bool changed = false;
	bool castsShadows = light.CastsShadows();
	if (ImGui::Checkbox("Casts Shadows", &castsShadows))
	{
		auto set = light.SetCastsShadows(castsShadows);
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	Float32 strength = light.ShadowStrength();
	if (ImGui::DragFloat("Shadow Strength", &strength, 0.01f, 0.0f, 1.0f))
	{
		auto set = light.SetShadowStrength(strength);
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	Float32 bias = light.ShadowBias();
	if (ImGui::DragFloat("Shadow Bias", &bias, 0.0001f, 0.0f, 1.0f))
	{
		auto set = light.SetShadowBias(bias);
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	Float32 normalBias = light.ShadowNormalBias();
	if (ImGui::DragFloat("Shadow Normal Bias", &normalBias, 0.001f, 0.0f, 10.0f))
	{
		auto set = light.SetShadowNormalBias(normalBias);
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	bool softShadows = light.SoftShadows();
	if (ImGui::Checkbox("Soft Shadows (3x3 PCF)", &softShadows))
	{
		auto set = light.SetSoftShadows(softShadows);
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	return changed;
}

bool AcceptAssetDrop(const Vector<ImportedArtifactRecord>& choices, AssetReference& reference,
                     const std::function<bool(const ImportedArtifactRecord&)>& compatible = {})
{
	bool accepted = false;
	if (ImGui::BeginDragDropTarget())
	{
		if (const auto* payload = ImGui::AcceptDragDropPayload("URSA_ASSET_REFERENCE");
		    payload && payload->DataSize == sizeof(AssetReference))
		{
			const auto dropped = *static_cast<const AssetReference*>(payload->Data);
			const auto object =
			    std::find_if(choices.begin(), choices.end(),
			                 [&](const auto& item)
			                 {
				                 return item.guid == dropped.guid && (item.localFileId == dropped.localFileId ||
				                                                      (!dropped.localFileId && item.isMainObject));
			                 });
			if (object != choices.end() && (!compatible || compatible(*object)))
			{
				reference = dropped;
				accepted = true;
			}
		}
		ImGui::EndDragDropTarget();
	}
	return accepted;
}

bool DrawSerializedAssetReference(nlohmann::json& value, const ProjectWorkspace& workspace)
{
	AssetReference current;
	if (value.is_object())
	{
		const auto parsed = AssetReferenceFromJson(value, "Inspector asset");
		if (parsed)
		{
			current = parsed.value;
		}
	}
	const auto choices = AssetObjectChoices(workspace);
	String preview = current.guid ? String("Missing asset") : String("None");
	for (const auto& object : choices)
	{
		if (object.guid == current.guid &&
		    (object.localFileId == current.localFileId || (!current.localFileId && object.isMainObject)))
		{
			preview = object.Label();
			break;
		}
	}
	bool changed = false;
	if (ImGui::BeginCombo("##Asset", preview.c_str()))
	{
		if (Icons::Selectable(Icons::Icon::Close, "None", !current.guid))
		{
			value = nullptr;
			changed = true;
		}
		for (const auto& object : choices)
		{
			const String key = object.guid.ToString() + ":" + ToUrsaString(std::to_string(object.localFileId));
			ImGui::PushID(key.c_str());
			if (Icons::Selectable(Icons::Icon::Link, (object.Label() + " - " + object.sourcePath.ToString()).c_str(),
			                      object.guid == current.guid && object.localFileId == current.localFileId))
			{
				value = AssetReferenceToJson({object.guid, object.localFileId});
				changed = true;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	AssetReference dropped;
	if (AcceptAssetDrop(choices, dropped))
	{
		value = AssetReferenceToJson(dropped);
		changed = true;
	}
	return changed;
}

bool DrawSerializedScriptableObjectReference(nlohmann::json& value, const String& expectedType,
                                             const ProjectWorkspace& workspace,
                                             const Vector<ScriptTypeDescriptor>* scriptTypes)
{
	AssetReference current;
	if (value.is_object() && value.contains("$ursaReference"))
	{
		const auto& ref = value["$ursaReference"];
		if (ref.contains("Resource") && ref["Resource"].contains("asset"))
		{
			const auto parsed = AssetReferenceFromJson(ref["Resource"]["asset"], "Inspector ScriptableObject");
			if (parsed)
			{
				current = parsed.value;
			}
		}
	}
	const auto choices = AssetObjectChoices(workspace);
	String preview = current.guid ? String("Missing asset") : String("None");
	for (const auto& object : choices)
	{
		if (object.guid == current.guid &&
		    (object.localFileId == current.localFileId || (!current.localFileId && object.isMainObject)))
		{
			preview = object.Label();
			break;
		}
	}
	auto resources = ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver());
	const auto compatible = [&](const ImportedArtifactRecord& object)
	{
		if (!resources)
		{
			return false;
		}
		if (object.kind != "ScriptableObject" && object.kind != "ScriptableAsset")
		{
			return false;
		}
		auto graph = LoadScriptableAssetGraph(*resources.value, {object.guid, object.localFileId});
		if (!graph)
		{
			return false;
		}
		const auto asset = std::find_if(graph.value.assets.begin(), graph.value.assets.end(),
		                                [&](const auto& item)
		                                {
			                                return item.SourceGuid() == graph.value.root.guid &&
			                                       item.LocalFileId() == graph.value.root.localFileId;
		                                });
		if (asset == graph.value.assets.end())
		{
			return false;
		}
		if (!expectedType.empty())
		{
			if (!scriptTypes)
			{
				return false;
			}
			const auto candidate = std::find_if(
			    scriptTypes->begin(), scriptTypes->end(), [&](const auto& type)
			    { return type.kind == ScriptTypeKind::ScriptableObject && type.scriptType == asset->TypeId(); });
			if (candidate == scriptTypes->end())
			{
				return false;
			}
			const Size separator = candidate->clrTypeName.find(',');
			const String concreteType = candidate->clrTypeName.substr(0, separator);
			if (concreteType != expectedType &&
			    std::find(candidate->baseTypeNames.begin(), candidate->baseTypeNames.end(), expectedType) ==
			        candidate->baseTypeNames.end())
			{
				return false;
			}
		}
		return true;
	};
	const auto assign = [&](AssetReference reference)
	{
		value = {{"$ursaReference",
		          {{"Kind", "ScriptableObject"},
		           {"Id", ""},
		           {"Resource", {{"kind", "asset"}, {"asset", AssetReferenceToJson(reference)}}}}}};
	};
	bool changed = false;
	if (ImGui::BeginCombo("##ScriptableAsset", preview.c_str()))
	{
		if (Icons::Selectable(Icons::Icon::Close, "None", !current.guid))
		{
			value = nullptr;
			changed = true;
		}
		for (const auto& object : choices)
		{
			if (!compatible(object))
			{
				continue;
			}
			const auto key = object.guid.ToString() + ":" + ToUrsaString(std::to_string(object.localFileId));
			ImGui::PushID(key.c_str());
			if (Icons::Selectable(Icons::Icon::Link, (object.Label() + " - " + object.sourcePath.ToString()).c_str(),
			                      object.guid == current.guid && (object.localFileId == current.localFileId ||
			                                                      (!current.localFileId && object.isMainObject))))
			{
				assign({object.guid, object.localFileId});
				changed = true;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	AssetReference dropped;
	if (AcceptAssetDrop(choices, dropped, compatible))
	{
		assign(dropped);
		changed = true;
	}
	return changed;
}

bool DrawSerializedScriptValue(nlohmann::json& value, const String& kind, const String& elementKind,
                               const nlohmann::json& descriptor, const ProjectWorkspace& workspace,
                               const ComponentInspectorContext& context,
                               const Vector<ScriptTypeDescriptor>* scriptTypes)
{
	if (kind == "Boolean")
	{
		bool candidate = value.is_boolean() && value.get<bool>();
		if (ImGui::Checkbox("##Value", &candidate))
		{
			value = candidate;
			return true;
		}
	}
	else if (kind == "SignedInteger")
	{
		Int64 candidate = value.is_number_integer() ? value.get<Int64>() : 0;
		if (ImGui::InputScalar("##Value", ImGuiDataType_S64, &candidate))
		{
			value = candidate;
			return true;
		}
	}
	else if (kind == "UnsignedInteger")
	{
		UInt64 candidate = value.is_number_unsigned() ? value.get<UInt64>() : 0;
		if (ImGui::InputScalar("##Value", ImGuiDataType_U64, &candidate))
		{
			value = candidate;
			return true;
		}
	}
	else if (kind == "FloatingPoint")
	{
		Float64 candidate = value.is_number() ? value.get<Float64>() : 0.0;
		if (ImGui::InputScalar("##Value", ImGuiDataType_Double, &candidate, nullptr, nullptr, "%.6g"))
		{
			value = candidate;
			return true;
		}
	}
	else if (kind == "String")
	{
		Ursa::Array<char, 1024> candidate{};
		if (value.is_string())
		{
			const std::string text = value.get<std::string>();
			std::memcpy(candidate.data(), text.data(), std::min(text.size(), candidate.size() - 1));
		}
		if (ImGui::InputText("##Value", candidate.data(), candidate.size()))
		{
			value = candidate.data();
			return true;
		}
	}
	else if (kind == "Enumeration" && descriptor.contains("EnumNumericValues"))
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
		if (ImGui::BeginCombo("##Value", preview.c_str()))
		{
			for (Size index = 0; index < names.size() && index < numbers.size(); ++index)
			{
				if (ImGui::Selectable(names[index].get<std::string>().c_str(), numbers[index] == value))
				{
					value = numbers[index];
					ImGui::EndCombo();
					return true;
				}
			}
			ImGui::EndCombo();
		}
	}
	else if (kind == "Vector2" || kind == "Vector3" || kind == "Vector4" || kind == "Quaternion" || kind == "Color")
	{
		const Size count = kind == "Vector2" ? 2 : kind == "Vector3" ? 3 : 4;
		Float32 components[4]{0.0f, 0.0f, 0.0f, 1.0f};
		for (Size index = 0; value.is_array() && index < count && index < value.size(); ++index)
		{
			if (value[index].is_number())
			{
				components[index] = value[index].get<Float32>();
			}
		}
		const bool edited = kind == "Vector2"   ? ImGui::DragFloat2("##Value", components, 0.05f)
		                    : kind == "Vector3" ? ImGui::DragFloat3("##Value", components, 0.05f)
		                    : kind == "Color"   ? ImGui::ColorEdit4("##Value", components)
		                                        : ImGui::DragFloat4("##Value", components, 0.005f);
		if (edited)
		{
			value = nlohmann::json::array();
			for (Size index = 0; index < count; ++index)
			{
				value.push_back(components[index]);
			}
			return true;
		}
	}
	else if (kind == "AssetReference")
	{
		return DrawSerializedAssetReference(value, workspace);
	}
	else if (kind == "ScriptableObjectReference")
	{
		const bool collection =
		    descriptor.value("Kind", std::string{}) == "Array" || descriptor.value("Kind", std::string{}) == "List";
		const String expectedType =
		    ToUrsaString(descriptor.value(collection ? "ElementTypeName" : "TypeName", std::string{}));
		return DrawSerializedScriptableObjectReference(value, expectedType, workspace, scriptTypes);
	}
	else if (kind == "MeshReference" || kind == "MaterialReference")
	{
		const auto expected = kind == "MeshReference" ? ResourceKind::Mesh : ResourceKind::Material;
		String preview = "None / Local";
		if (value.is_object() && value.contains("$ursaReference") && value["$ursaReference"].contains("Resource"))
		{
			const auto& binding = value["$ursaReference"]["Resource"];
			if (binding.is_object() && binding.value("kind", std::string{}) == "asset")
			{
				preview = "Missing asset";
				const auto reference = AssetReferenceFromJson(binding["asset"], "Inspector resource");
				if (reference)
				{
					for (const auto& object : AssetObjectChoices(workspace))
					{
						if (object.guid == reference.value.guid &&
						    (object.localFileId == reference.value.localFileId ||
						     (!reference.value.localFileId && object.isMainObject)))
						{
							preview = object.Label();
							break;
						}
					}
				}
			}
		}
		const auto choices = AssetObjectChoices(workspace);
		auto resources = ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver());
		const auto compatible = [&](const ImportedArtifactRecord& object)
		{
			if (!resources || object.kind != (expected == ResourceKind::Mesh ? "Mesh" : "Material"))
			{
				return false;
			}
			return expected == ResourceKind::Mesh
			           ? bool(resources.value->LoadMesh(object.guid, object.localFileId))
			           : bool(resources.value->LoadMaterial(object.guid, object.localFileId));
		};
		const auto assign = [&](AssetReference reference)
		{
			value = {{"$ursaReference",
			          {{"Kind", expected == ResourceKind::Mesh ? "Mesh" : "Material"},
			           {"Id", ""},
			           {"Resource", {{"kind", "asset"}, {"asset", AssetReferenceToJson(reference)}}}}}};
		};
		bool changed = false;
		if (ImGui::BeginCombo("##Resource", preview.c_str()))
		{
			if (Icons::Selectable(Icons::Icon::Close, "None"))
			{
				value = nullptr;
				changed = true;
			}
			for (const auto& object : choices)
			{
				if (object.kind == (expected == ResourceKind::Mesh ? "Mesh" : "Material") &&
				    Icons::Selectable(Icons::Icon::Link,
				                      (object.Label() + " - " + object.sourcePath.ToString()).c_str()) &&
				    compatible(object))
				{
					assign({object.guid, object.localFileId});
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		AssetReference dropped;
		if (AcceptAssetDrop(choices, dropped, compatible))
		{
			assign(dropped);
			changed = true;
		}
		return changed;
	}
	else if (kind == "GameObjectReference" || kind == "ComponentReference")
	{
		return DrawSerializedReference(value, kind, context);
	}
	else if (kind == "Array" || kind == "List")
	{
		if (!value.is_array())
		{
			value = nlohmann::json::array();
		}
		bool changed = false;
		for (Size index = 0; index < value.size(); ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			ImGui::Text("[%u]", static_cast<unsigned int>(index));
			Icons::SameLine();
			changed |=
			    DrawSerializedScriptValue(value[index], elementKind, {}, descriptor, workspace, context, scriptTypes);
			Icons::SameLine();
			if (index > 0)
			{
				if (Icons::SmallButton("^"))
				{
					std::swap(value[index], value[index - 1]);
					changed = true;
					ImGui::PopID();
					break;
				}
				Icons::SameLine();
			}
			if (index + 1 < value.size())
			{
				if (Icons::SmallButton("v"))
				{
					std::swap(value[index], value[index + 1]);
					changed = true;
					ImGui::PopID();
					break;
				}
				Icons::SameLine();
			}
			if (Icons::SmallButton("-"))
			{
				value.erase(value.begin() + static_cast<std::ptrdiff_t>(index));
				changed = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (Icons::SmallButton("Add element"))
		{
			nlohmann::json item = nullptr;
			if (elementKind == "Boolean")
			{
				item = false;
			}
			else if (elementKind == "SignedInteger")
			{
				item = Int64{0};
			}
			else if (elementKind == "UnsignedInteger")
			{
				item = UInt64{0};
			}
			else if (elementKind == "FloatingPoint")
			{
				item = 0.0;
			}
			else if (elementKind == "String")
			{
				item = "";
			}
			else if (elementKind == "Vector2")
			{
				value.push_back(nlohmann::json::array({0.0f, 0.0f}));
			}
			else if (elementKind == "Vector3")
			{
				item = {0.0, 0.0, 0.0};
			}
			else if (elementKind == "Vector4" || elementKind == "Quaternion" || elementKind == "Color")
			{
				item = {0.0, 0.0, 0.0, 0.0};
			}
			else if (elementKind == "Enumeration" && descriptor.contains("EnumNumericValues") &&
			         !descriptor["EnumNumericValues"].empty())
			{
				item = descriptor["EnumNumericValues"][0];
			}
			value.push_back(std::move(item));
			changed = true;
		}
		return changed;
	}
	else
	{
		ImGui::TextDisabled("Unsupported field type: %s", kind.c_str());
	}
	return false;
}

bool DrawSerializedScriptFields(Scripting::ScriptBehaviourComponent& component, const ProjectWorkspace& workspace,
                                const ComponentInspectorContext& context, String& diagnostic,
                                const Vector<ScriptTypeDescriptor>* scriptTypes)
{
	if (scriptTypes)
	{
		try
		{
			const nlohmann::json payload = nlohmann::json::parse(ToStdString(component.SerializedPayload()));
			const String scriptType = ToUrsaString(payload.value("ScriptType", std::string{}));
			const bool found = std::any_of(scriptTypes->begin(), scriptTypes->end(),
			                               [&scriptType](const ScriptTypeDescriptor& descriptor)
			                               { return descriptor.scriptType == scriptType; });
			if (!found)
			{
				diagnostic = "Missing Script: " + scriptType + ". Serialized data is preserved.";
				ImGui::TextWrapped("%s", diagnostic.c_str());
				return false;
			}
		}
		catch (const nlohmann::json::exception& error)
		{
			diagnostic = "Invalid Script payload: " + ToUrsaString(error.what());
			ImGui::TextWrapped("%s", diagnostic.c_str());
			return false;
		}
	}
	auto serialized =
	    SerializedObject::Create(component, context.loadContext ? &context.loadContext->Resources() : nullptr);
	if (!serialized)
	{
		diagnostic = serialized.error;
		ImGui::TextDisabled("Script fields are unavailable; serialized payload is preserved.");
		return false;
	}
	nlohmann::json payload;
	try
	{
		payload = nlohmann::json::parse(ToStdString(component.SerializedPayload()));
	}
	catch (const nlohmann::json::exception& error)
	{
		diagnostic = ToUrsaString(error.what());
		return false;
	}
	if (!payload.contains("Fields") || !payload["Fields"].is_object() || !payload.contains("FieldTypes") ||
	    !payload["FieldTypes"].is_object())
	{
		ImGui::TextDisabled("This script has no available field schema.");
		return false;
	}
	bool staged = false;
	for (const auto& field : payload["Fields"].items())
	{
		const String name = ToUrsaString(field.key());
		if (!payload["FieldTypes"].contains(field.key()))
		{
			ImGui::TextDisabled("%s (unknown saved field; preserved)", name.c_str());
			continue;
		}
		const auto& descriptor = payload["FieldTypes"][field.key()];
		if (!descriptor.is_object() || !descriptor.contains("Kind") || !descriptor["Kind"].is_string())
		{
			ImGui::TextDisabled("%s (invalid field descriptor)", name.c_str());
			continue;
		}
		String kind = ToUrsaString(descriptor["Kind"].get<std::string>());
		String elementKind;
		if (descriptor.contains("ElementKind") && descriptor["ElementKind"].is_string())
		{
			elementKind = ToUrsaString(descriptor["ElementKind"].get<std::string>());
		}
		auto property = serialized.value->FindProperty(String("/Fields/") + name);
		if (!property)
		{
			ImGui::TextDisabled("%s (unavailable)", name.c_str());
			continue;
		}
		ImGui::PushID(name.c_str());
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(name.c_str());
		ImGui::SameLine(150.0f);
		ImGui::SetNextItemWidth(-1.0f);
		nlohmann::json candidate = field.value();
		if (DrawSerializedScriptValue(candidate, kind, elementKind, descriptor, workspace, context, scriptTypes))
		{
			auto set = property.value.SetValue(std::move(candidate));
			if (!set)
			{
				diagnostic = set.error;
			}
			else
			{
				staged = true;
			}
		}
		ImGui::PopID();
	}
	if (!staged)
	{
		return false;
	}
	auto applied = serialized.value->Apply();
	if (!applied)
	{
		diagnostic = applied.error;
		return false;
	}
	return true;
}
} // namespace

Result<bool> DrawSceneInspector(EditorDocument& document, EditorHierarchy& hierarchy, EditorDocuments& documents,
                                ProjectWorkspace& workspace, ComponentRegistry& registry,
                                ComponentEditors& componentEditors, String& diagnostic,
                                const Vector<ScriptTypeDescriptor>* scriptTypes, bool* hierarchyOpen,
                                bool* inspectorOpen, bool drawInspector)
{
	auto draft = document.Payload<SceneDraft>();
	if (!draft)
	{
		return Result<bool>::Failure("Scene document has no scene draft");
	}
	if (draft->Kind() == ResourceKind::Prefab)
	{
		auto* asset = draft->GetPrefabAsset();
		if (!asset)
		{
			return Result<bool>::Failure("Prefab document has no detached PrefabAsset");
		}
		auto hierarchyChanged = hierarchy.Draw(*asset, hierarchyOpen);
		if (!hierarchyChanged)
		{
			return hierarchyChanged;
		}
		bool changed = hierarchyChanged.value;
		auto selected = hierarchy.SelectedGameObject(*asset);
		if (drawInspector)
		{
			ImGui::Begin("Inspector", inspectorOpen);
			if (!selected)
			{
				ImGui::TextDisabled("Select a GameObject in the Prefab hierarchy.");
			}
			else
			{
				GameObject::Ptr object = selected.value;
				ComponentLoadContext loadContext(documents.Session());
				if (draft->ContextSetup())
				{
					auto prepared = draft->ContextSetup()(loadContext);
					if (!prepared)
					{
						diagnostic = prepared.error;
					}
				}
				ComponentInspectorContext inspectorContext;
				inspectorContext.object = object;
				inspectorContext.roots = {asset->Root()};
				inspectorContext.prefab = asset;
				inspectorContext.loadContext = &loadContext;
				Icons::Section(Icons::ForObject(object, object == asset->Root()), "GameObject");
				String name = object->Name().value;
				char nameBuffer[257]{};
				std::memcpy(nameBuffer, name.data(), std::min<Ursa::Size>(name.size(), sizeof(nameBuffer) - 1));
				if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
				{
					auto renamed = object->SetName(String(nameBuffer));
					diagnostic = renamed ? String{} : renamed.error;
					changed |= static_cast<bool>(renamed);
				}
				bool active = object->ActiveSelf().value;
				if (ImGui::Checkbox("Active", &active))
				{
					auto set = object->SetActive(active);
					diagnostic = set ? String{} : set.error;
					changed |= static_cast<bool>(set);
				}
				if (Icons::Button("Add child"))
				{
					auto child = GameObject::Create("GameObject");
					auto parented = child->SetParent(object);
					diagnostic = parented ? String{} : parented.error;
					changed |= static_cast<bool>(parented);
				}
				Icons::SameLine();
				ImGui::BeginDisabled(!object->Parent());
				if (Icons::Button("Delete GameObject"))
				{
					auto removed = object->Destroy();
					diagnostic = removed ? String{} : removed.error;
					if (removed)
					{
						hierarchy.ClearSelection();
						selected = Result<GameObject::Ptr>::Failure("Deleted selection");
						changed = true;
					}
				}
				ImGui::EndDisabled();
				auto local = object->GetLocalTransform();
				if (local)
				{
					Icons::Section(Icons::Icon::Move, "Transform");
					LocalTransform transform = local.value;
					Ursa::Float32 position[3]{transform.position.x, transform.position.y, transform.position.z};
					Ursa::Float32 scale[3]{transform.scale.x, transform.scale.y, transform.scale.z};
					bool edited = ImGui::DragFloat3("Position", position, 0.05f);
					edited |= ImGui::DragFloat3("Scale", scale, 0.05f, 0.001f, 100.f);
					if (edited)
					{
						transform.position = {position[0], position[1], position[2]};
						transform.scale = {scale[0], scale[1], scale[2]};
						auto set = object->SetLocalTransform(transform);
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
				}
				for (Component* component : object->Components())
				{
					ImGui::PushID(component->PersistentId().ToString().c_str());
					auto codec = registry.FindFor(*component);
					const auto* scriptHeader = dynamic_cast<Scripting::ScriptBehaviourComponent*>(component);
					const auto* descriptor = scriptHeader ? componentEditors.FindType(scriptHeader->TypeName())
					                         : codec      ? componentEditors.FindType(codec.value->type)
					                                      : nullptr;
					Icons::Inline(Icons::ForComponent(component->SerializationType(), scriptHeader != nullptr));
					ImGui::SeparatorText(descriptor     ? descriptor->displayName.c_str()
					                     : scriptHeader ? scriptHeader->TypeName().c_str()
					                     : codec        ? codec.value->type.c_str()
					                                    : "Component");
					bool componentEnabled = component->IsEnabled();
					if (component->SupportsEnabled() && ImGui::Checkbox("Enabled", &componentEnabled))
					{
						component->SetEnabled(componentEnabled);
						changed = true;
					}
					Icons::SameLine();
					const ComponentEditor* customEditor = componentEditors.Find(scriptHeader ? scriptHeader->TypeName()
					                                                            : codec      ? codec.value->type
					                                                                         : String{});
					bool customDrawn = false;
					if (customEditor)
					{
						ImGuiInspectorUI inspectorUi;
						auto edited = (*customEditor)(*component, inspectorContext, inspectorUi);
						if (edited)
						{
							changed |= edited.value;
							customDrawn = true;
						}
						else
						{
							diagnostic = edited.error;
						}
					}
					if (customDrawn)
					{
					}
					else if (auto* scriptData = dynamic_cast<Scripting::ScriptBehaviourComponent*>(component))
					{
						changed |= DrawSerializedScriptFields(*scriptData, workspace, inspectorContext, diagnostic,
						                                      scriptTypes);
					}
					else if (HasMeshInspector(*component))
					{
						auto edited = DrawMeshInspector(*component, workspace, documents.Session());
						if (edited)
						{
							changed |= edited.value;
						}
						else
						{
							diagnostic = edited.error;
						}
					}
					else if (auto* camera = dynamic_cast<CameraComponent*>(component))
					{
						Ursa::Float32 fov = camera->verticalFovRadians * 57.2957795f;
						bool edited = ImGui::DragFloat("FOV degrees", &fov, 0.5f, 1.f, 179.f);
						edited |= ImGui::DragFloat("Near", &camera->nearPlane, 0.01f, 0.001f, 10000.f);
						edited |= ImGui::DragFloat("Far", &camera->farPlane, 0.1f, 0.002f, 100000.f);
						if (edited && fov > 0.f && fov < 180.f)
						{
							camera->verticalFovRadians = fov * 0.0174532925f;
							changed = true;
						}
					}
					else if (auto* light = dynamic_cast<DirectionalLightComponent*>(component))
					{
						Ursa::Float32 intensity = light->Intensity();
						if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.f, 10000.f))
						{
							light->SetIntensity(intensity);
							changed = true;
						}
						changed |= DrawDirectionalShadowControls(*light, diagnostic);
					}
					else if (auto* pointLight = dynamic_cast<PointLightComponent*>(component))
					{
						Ursa::Float32 intensity = pointLight->Intensity();
						Ursa::Float32 range = pointLight->Range();
						bool edited = ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.f, 10000.f);
						edited |= ImGui::DragFloat("Range", &range, 0.05f, 0.001f, 10000.f);
						if (edited)
						{
							pointLight->SetIntensity(intensity);
							pointLight->SetRange(range);
							changed = true;
						}
					}
					else if (codec && componentEditors.Find(codec.value->type))
					{
						ImGuiInspectorUI inspectorUi;
						auto edited =
						    (*componentEditors.Find(codec.value->type))(*component, inspectorContext, inspectorUi);
						diagnostic = edited ? String{} : edited.error;
						changed |= edited && edited.value;
					}
					else
					{
						ImGui::TextDisabled("No property editor registered; serialized data is preserved");
					}
					if (Icons::ComponentMenu())
					{
						auto removed = object->RemoveComponent(component);
						diagnostic = removed ? String{} : removed.error;
						changed |= static_cast<bool>(removed);
					}
					ImGui::PopID();
				}
				changed |= DrawAddComponentBrowser(componentEditors, inspectorContext, diagnostic, scriptTypes);
			}
			if (!diagnostic.empty())
			{
				ImGui::TextWrapped("%s", diagnostic.c_str());
			}
			ImGui::End();
		}
		if (changed)
		{
			auto refreshed = draft->RefreshPrefabPreview();
			diagnostic = refreshed ? String{} : refreshed.error;
			document.MarkDirty();
		}
		return Result<bool>::Success(changed);
	}
	Scene& scene = draft->GetScene();
	auto hierarchyChanged = hierarchy.Draw(scene, hierarchyOpen, draft->LinkedScene());
	if (!hierarchyChanged)
	{
		return hierarchyChanged;
	}
	bool changed = hierarchyChanged.value;
	ResourceId prefabOperation;
	bool applyPrefab = false;
	ResourceId prefabToPlace;
	ResourceId placementParent;
	auto selected = hierarchy.SelectedGameObject(scene);
	ComponentLoadContext loadContext(documents.Session());
	if (selected && draft->ContextSetup())
	{
		auto prepared = draft->ContextSetup()(loadContext);
		if (!prepared)
		{
			diagnostic = prepared.error;
		}
	}
	ComponentInspectorContext inspectorContext;
	if (selected)
	{
		inspectorContext.object = selected.value;
		inspectorContext.roots = scene.RootGameObjects();
		inspectorContext.scene = &scene;
		inspectorContext.loadContext = &loadContext;
	}
	if (drawInspector)
	{
		ImGui::Begin("Inspector", inspectorOpen);
		if (!selected)
		{
			ImGui::TextDisabled("Select a node in Hierarchy to edit its properties.");
		}
		else
		{
			GameObject::Ptr node = selected.value;
			Icons::Section(Icons::ForObject(node, hierarchy.IsPrefabObject(node->Id())), "GameObject");
			auto name = scene.GameObjectName(node);
			if (!name)
			{
				ImGui::End();
				return Result<bool>::Failure(name.error);
			}
			char nameBuffer[257]{};
			std::memcpy(nameBuffer, name.value.data(), std::min<Ursa::Size>(name.value.size(), sizeof(nameBuffer) - 1));
			if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
			{
				auto renamed = scene.SetGameObjectName(node, String(nameBuffer));
				diagnostic = renamed ? String{} : renamed.error;
				changed |= static_cast<bool>(renamed);
			}
			auto activeState = scene.ActiveSelf(node);
			if (!activeState)
			{
				ImGui::End();
				return Result<bool>::Failure(activeState.error);
			}
			bool active = activeState.value;
			if (ImGui::Checkbox("Active", &active))
			{
				auto set = scene.SetActive(node, active);
				diagnostic = set ? String{} : set.error;
				changed |= static_cast<bool>(set);
			}
		}
		if (draft->Kind() == ResourceKind::Scene && ImGui::BeginCombo("Place Prefab", "Select source"))
		{
			for (const auto& entry : workspace.Resources())
			{
				if (entry.location.kind == ResourceKind::Prefab)
				{
					ImGui::PushID(entry.id.ToString().c_str());
					const auto name = entry.location.path.FileName();
					if (ImGui::Selectable(name.c_str()))
					{
						prefabToPlace = entry.id;
						placementParent = selected ? selected.value->Id() : ResourceId{};
					}
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}
		if (selected)
		{
			GameObject::Ptr node = selected.value;
			if (draft->Linked())
			{
				auto selectedId = scene.PersistentId(node);
				if (selectedId)
				{
					for (const auto& instance : draft->LinkedScene()->scopedIds)
					{
						for (const auto& mapping : instance.second)
						{
							if (mapping.second == selectedId.value.ToString())
							{
								ImGui::Text("Prefab instance: %s", instance.first.c_str());
								if (Icons::Button("Apply Prefab"))
								{
									prefabOperation = ResourceId::Parse(instance.first).value;
									applyPrefab = true;
								}
								Icons::SameLine();
								if (Icons::Button("Revert Prefab"))
								{
									prefabOperation = ResourceId::Parse(instance.first).value;
									applyPrefab = false;
								}
								if (Icons::Button("Pin label override"))
								{
									auto instanceId = ResourceId::Parse(instance.first);
									auto sourceId = ResourceId::Parse(mapping.first);
									auto label = scene.GameObjectName(node);
									if (instanceId && sourceId && label)
									{
										auto pinned = draft->SetInstanceProperty(instanceId.value, sourceId.value,
										                                         "/name", ToStdString(label.value));
										diagnostic = pinned ? String{} : pinned.error;
										changed |= static_cast<bool>(pinned);
									}
								}
								break;
							}
						}
					}
				}
			}
			if (Icons::Button("Add child"))
			{
				auto added = scene.CreateGameObject(node);
				diagnostic = added ? String{} : added.error;
				changed |= static_cast<bool>(added);
			}
			Icons::SameLine();
			if (Icons::Button("Delete node"))
			{
				auto removed = scene.DestroyGameObject(node);
				diagnostic = removed ? String{} : removed.error;
				if (removed)
				{
					hierarchy.ClearSelection();
					changed = true;
				}
			}
			if (scene.Contains(node))
			{
				auto parent = scene.ParentOf(node);
				if (parent)
				{
					Vector<GameObject::Ptr> siblings;
					if (parent.value)
					{
						auto children = scene.ChildrenOf(parent.value);
						if (children)
						{
							siblings = std::move(children.value);
						}
					}
					else
					{
						siblings = scene.RootGameObjects();
					}
					const auto position = std::find(siblings.begin(), siblings.end(), node);
					if (position != siblings.end())
					{
						const Ursa::Size index = static_cast<Ursa::Size>(position - siblings.begin());
						ImGui::BeginDisabled(index == 0);
						if (Icons::Button("Move up"))
						{
							auto moved = scene.ReorderGameObject(node, index - 1);
							diagnostic = moved ? String{} : moved.error;
							changed |= static_cast<bool>(moved);
						}
						ImGui::EndDisabled();
						Icons::SameLine();
						ImGui::BeginDisabled(index + 1 >= siblings.size());
						if (Icons::Button("Move down"))
						{
							auto moved = scene.ReorderGameObject(node, index + 1);
							diagnostic = moved ? String{} : moved.error;
							changed |= static_cast<bool>(moved);
						}
						ImGui::EndDisabled();
					}
					if (ImGui::BeginCombo("Parent", parent.value ? "Node" : "Scene root"))
					{
						if (ImGui::Selectable("Scene root", !parent.value))
						{
							auto moved = scene.SetParent(node, {});
							diagnostic = moved ? String{} : moved.error;
							changed |= static_cast<bool>(moved);
						}
						std::function<void(GameObject::Ptr)> offer = [&](GameObject::Ptr candidate)
						{
							if (candidate != node)
							{
								auto label = scene.GameObjectName(candidate);
								auto id = scene.PersistentId(candidate);
								if (label && id)
								{
									ImGui::PushID(id.value.ToString().c_str());
									if (ImGui::Selectable(label.value.c_str(), parent.value == candidate))
									{
										auto moved = scene.SetParent(node, candidate);
										diagnostic = moved ? String{} : moved.error;
										changed |= static_cast<bool>(moved);
									}
									ImGui::PopID();
								}
							}
							auto children = scene.ChildrenOf(candidate);
							if (children)
							{
								for (GameObject::Ptr child : children.value)
								{
									offer(child);
								}
							}
						};
						for (GameObject::Ptr root : scene.RootGameObjects())
						{
							offer(root);
						}
						ImGui::EndCombo();
					}
				}
				const auto local = scene.GetLocalTransform(node);
				if (local)
				{
					Icons::Section(Icons::Icon::Move, "Transform");
					LocalTransform value = local.value;
					Ursa::Float32 position[3]{value.position.x, value.position.y, value.position.z};
					Ursa::Float32 scale[3]{value.scale.x, value.scale.y, value.scale.z};
					bool transformChanged = ImGui::DragFloat3("Position", position, 0.05f);
					transformChanged |= ImGui::DragFloat3("Scale", scale, 0.05f, 0.001f, 100.f);
					if (transformChanged)
					{
						value.position = {position[0], position[1], position[2]};
						value.scale = {scale[0], scale[1], scale[2]};
						try
						{
							auto transformParent = scene.ParentOf(node);
							const Mat4 parentWorld = transformParent && transformParent.value
							                             ? scene.WorldTransform(transformParent.value).value
							                             : Mat4::Identity();
							auto applied = ApplyGizmoWorldTransform(scene, node, parentWorld * value.Matrix());
							diagnostic = applied ? String{} : applied.error;
							changed |= static_cast<bool>(applied);
						}
						catch (const std::exception& error)
						{
							diagnostic = error.what();
						}
					}
				}
				auto components = scene.ComponentsOf(node);
				if (!components)
				{
					ImGui::End();
					return Result<bool>::Failure(components.error);
				}
				for (ComponentId componentId : components.value)
				{
					Component* object = scene.GetComponent(componentId);
					if (!object)
					{
						continue;
					}
					auto id = scene.ComponentPersistentId(componentId);
					if (!id)
					{
						ImGui::End();
						return Result<bool>::Failure(id.error);
					}
					ImGui::PushID(id.value.ToString().c_str());
					auto codec = registry.FindFor(*object);
					if (!codec)
					{
						diagnostic = codec.error;
						ImGui::PopID();
						continue;
					}
					auto* scriptHeader = dynamic_cast<Scripting::ScriptBehaviourComponent*>(object);
					const auto* descriptor = scriptHeader ? componentEditors.FindType(scriptHeader->TypeName())
					                                      : componentEditors.FindType(codec.value->type);
					Icons::Inline(Icons::ForComponent(object->SerializationType(), scriptHeader != nullptr));
					ImGui::SeparatorText(descriptor     ? descriptor->displayName.c_str()
					                     : scriptHeader ? scriptHeader->TypeName().c_str()
					                                    : codec.value->type.c_str());
					auto enabled = scene.IsComponentEnabled(componentId);
					if (enabled && object->SupportsEnabled())
					{
						bool value = enabled.value;
						if (ImGui::Checkbox("Enabled", &value))
						{
							auto set = scene.SetComponentEnabled(componentId, value);
							diagnostic = set ? String{} : set.error;
							changed |= static_cast<bool>(set);
						}
					}
					Icons::SameLine();
					const bool remove = Icons::ComponentMenu();
					if (remove)
					{
						auto removed = scene.RemoveComponent(componentId);
						diagnostic = removed ? String{} : removed.error;
						changed |= static_cast<bool>(removed);
						ImGui::PopID();
						continue;
					}
					const ComponentEditor* customEditor = componentEditors.Find(scriptHeader ? scriptHeader->TypeName()
					                                                            : codec      ? codec.value->type
					                                                                         : String{});
					bool customDrawn = false;
					if (customEditor)
					{
						ImGuiInspectorUI inspectorUi;
						auto edited = (*customEditor)(*object, inspectorContext, inspectorUi);
						if (edited)
						{
							changed |= edited.value;
							customDrawn = true;
						}
						else
						{
							diagnostic = edited.error;
						}
					}
					if (customDrawn)
					{
					}
					else if (auto* scriptData = dynamic_cast<Scripting::ScriptBehaviourComponent*>(object))
					{
						changed |= DrawSerializedScriptFields(*scriptData, workspace, inspectorContext, diagnostic,
						                                      scriptTypes);
					}
					else if (HasMeshInspector(*object))
					{
						auto edited = DrawMeshInspector(*object, workspace, documents.Session());
						if (edited)
						{
							changed |= edited.value;
						}
						else
						{
							diagnostic = edited.error;
						}
					}
					else if (auto* camera = dynamic_cast<CameraComponent*>(object))
					{
						Ursa::Float32 fov = camera->verticalFovRadians * 57.2957795f;
						Ursa::Float32 nearPlane = camera->nearPlane;
						Ursa::Float32 farPlane = camera->farPlane;
						bool edited = ImGui::DragFloat("FOV degrees", &fov, 0.5f, 1.f, 179.f);
						edited |= ImGui::DragFloat("Near", &nearPlane, 0.01f, 0.001f, 10000.f);
						edited |= ImGui::DragFloat("Far", &farPlane, 0.1f, 0.002f, 100000.f);
						if (edited)
						{
							if (std::isfinite(fov) && std::isfinite(nearPlane) && std::isfinite(farPlane) &&
							    fov > 0.f && fov < 180.f && nearPlane > 0.f && farPlane > nearPlane)
							{
								camera->verticalFovRadians = fov * 0.0174532925f;
								camera->nearPlane = nearPlane;
								camera->farPlane = farPlane;
								changed = true;
								diagnostic.clear();
							}
							else
							{
								diagnostic = "Camera requires 0 < FOV < 180 and 0 < Near < Far";
							}
						}
						if (Icons::Button("Use as game camera"))
						{
							auto set = scene.SetActiveCamera(componentId);
							diagnostic = set ? String{} : set.error;
							changed |= static_cast<bool>(set);
						}
					}
					else if (auto* directional = dynamic_cast<DirectionalLightComponent*>(object))
					{
						Color value = directional->Color();
						Ursa::Float32 color[4]{value.red, value.green, value.blue, value.alpha};
						if (ImGui::ColorEdit4("Color", color))
						{
							auto set = directional->SetColor({color[0], color[1], color[2], color[3]});
							diagnostic = set ? String{} : set.error;
							changed |= static_cast<bool>(set);
						}
						Ursa::Float32 intensity = directional->Intensity();
						if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.f, 10000.f))
						{
							auto set = directional->SetIntensity(intensity);
							diagnostic = set ? String{} : set.error;
							changed |= static_cast<bool>(set);
						}
						changed |= DrawDirectionalShadowControls(*directional, diagnostic);
					}
					else if (auto* point = dynamic_cast<PointLightComponent*>(object))
					{
						Color value = point->Color();
						Ursa::Float32 color[4]{value.red, value.green, value.blue, value.alpha};
						if (ImGui::ColorEdit4("Color", color))
						{
							auto set = point->SetColor({color[0], color[1], color[2], color[3]});
							diagnostic = set ? String{} : set.error;
							changed |= static_cast<bool>(set);
						}
						Ursa::Float32 intensity = point->Intensity();
						Ursa::Float32 range = point->Range();
						if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.f, 10000.f))
						{
							auto set = point->SetIntensity(intensity);
							diagnostic = set ? String{} : set.error;
							changed |= static_cast<bool>(set);
						}
						if (ImGui::DragFloat("Range", &range, 0.05f, 0.001f, 10000.f))
						{
							auto set = point->SetRange(range);
							diagnostic = set ? String{} : set.error;
							changed |= static_cast<bool>(set);
						}
					}
					else if (const auto* editor = componentEditors.Find(codec.value->type))
					{
						inspectorContext.object = node;
						ImGuiInspectorUI inspectorUi;
						auto edited = (*editor)(*object, inspectorContext, inspectorUi);
						diagnostic = edited ? String{} : edited.error;
						changed |= edited && edited.value;
					}
					else
					{
						ImGui::TextDisabled("No property editor registered; serialized data is preserved");
					}
					ImGui::PopID();
				}
				changed |= DrawAddComponentBrowser(componentEditors, inspectorContext, diagnostic, scriptTypes);
			}
		}
		if (!diagnostic.empty())
		{
			ImGui::TextWrapped("%s", diagnostic.c_str());
		}
		ImGui::End();
	}
	if (prefabOperation && draft->LinkedScene())
	{
		for (const auto& instance : draft->LinkedScene()->document["instances"])
		{
			if (instance["id"] != ToStdString(prefabOperation.ToString()))
			{
				continue;
			}
			auto reference = AssetReferenceFromJson(instance["prefab"], "Prefab");
			if (!reference)
			{
				diagnostic = reference.error;
				break;
			}
			for (const auto& asset : workspace.Assets())
			{
				if (asset.metadata.guid != reference.value.guid)
				{
					continue;
				}
				auto result = applyPrefab
				                  ? draft->ApplyInstance(workspace.Index().Filesystem(), asset.path, prefabOperation)
				                  : draft->RevertInstance(workspace.Index().Filesystem(), asset.path, prefabOperation);
				diagnostic = result ? String{} : result.error;
				if (result)
				{
					hierarchy.ClearSelection();
					changed = true;
				}
				break;
			}
			break;
		}
	}
	if (prefabToPlace)
	{
		auto placed = draft->PlacePrefab(prefabToPlace, placementParent, document.Path(), document.Id());
		diagnostic = placed ? String{} : placed.error;
		changed |= static_cast<bool>(placed);
	}
	if (changed)
	{
		document.MarkDirty();
	}
	return Result<bool>::Success(changed);
}

void DrawRuntimeSceneInspector(Scene& scene, EditorHierarchy& hierarchy, bool* inspectorOpen, String& diagnostic,
                               ProjectWorkspace* workspace, ResourceSession* resources)
{
	if (inspectorOpen && !*inspectorOpen)
	{
		return;
	}
	ImGui::Begin("Inspector", inspectorOpen);
	auto selected = hierarchy.SelectedGameObject(scene);
	if (!selected)
	{
		ImGui::TextDisabled("Select a runtime GameObject in Hierarchy.");
	}
	else
	{
		auto name = scene.GameObjectName(selected.value);
		auto active = scene.ActiveSelf(selected.value);
		auto transform = scene.GetLocalTransform(selected.value);
		if (!name || !active || !transform)
		{
			diagnostic = !name ? name.error : !active ? active.error : transform.error;
			ImGui::TextWrapped("%s", diagnostic.c_str());
		}
		else
		{
			Icons::Text(Icons::ForObject(selected.value, hierarchy.IsPrefabObject(selected.value->Id())),
			            name.value.c_str());
			ImGui::Separator();
			ImGui::Text("Active: %s", active.value ? "Yes" : "No");
			ImGui::Text("Position: %.3f, %.3f, %.3f", transform.value.position.x, transform.value.position.y,
			            transform.value.position.z);
			ImGui::Text("Rotation: %.3f, %.3f, %.3f, %.3f", transform.value.rotation.x, transform.value.rotation.y,
			            transform.value.rotation.z, transform.value.rotation.w);
			ImGui::Text("Scale: %.3f, %.3f, %.3f", transform.value.scale.x, transform.value.scale.y,
			            transform.value.scale.z);
			for (Component* component : selected.value->Components())
			{
				ImGui::PushID(component->PersistentId().ToString().c_str());
				Icons::Section(
				    Icons::ForComponent(component->SerializationType(),
				                        dynamic_cast<Scripting::ScriptBehaviourComponent*>(component) != nullptr),
				    component->SerializationType().c_str());
				if (component->SupportsEnabled())
				{
					bool enabled = component->IsEnabled();
					if (ImGui::Checkbox("Enabled", &enabled))
					{
						auto set = component->SetEnabled(enabled);
						if (!set)
						{
							diagnostic = set.error;
						}
					}
				}
				if (workspace && resources && HasMeshInspector(*component))
				{
					auto edited = DrawMeshInspector(*component, *workspace, *resources);
					if (!edited)
					{
						diagnostic = edited.error;
					}
				}
				ImGui::PopID();
			}
			ImGui::TextDisabled("Runtime changes are discarded when Play stops.");
		}
	}
	ImGui::End();
}
} // namespace Ursa::Editor
