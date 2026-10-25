#include <Assets/ProjectBrowserModel.hpp>
#include <Assets/AssetPreviewContent.hpp>
#include <Ui/EditorIcons.hpp>
#include <Inspector/SceneInspectorPanel.hpp>
#include <Inspector/ReferencePicker.hpp>
#include <Inspector/MeshInspector.hpp>

#include <Core/StringInterop.hpp>
#include <Ui/ImGuiInspectorUI.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/PrefabCapture.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Scene/ComponentEditors.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Scene/RenderingComponents.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>

namespace Ursa::Editor
{
namespace
{
using SerializedJson = nlohmann::json;
using GroupCommitObserver =
    std::function<Result<void>(const SerializedObjectGroup&, const String&, const Vector<bool>&)>;

Result<void> RestoreReferencePickerTransaction(const ReferencePickerTransaction& transaction, bool* transactionHandled,
                                               const SerializedJson& checkpoint);
ReferencePickerTransaction MapPickerValues(const ReferencePickerTransaction& transaction, bool* transactionHandled,
                                           Vector<SerializedJson> candidateValues);

bool DrawSerializedScriptValue(SerializedJson& value, const String& kind, const String& elementKind,
                               const SerializedJson& descriptor, ProjectWorkspace& workspace,
                               const ComponentInspectorContext& context,
                               const Vector<ScriptTypeDescriptor>* scriptTypes,
                               const ReferencePickerTransaction* transaction = nullptr,
                               bool* transactionHandled = nullptr);

struct PrefabOverrideTarget
{
	ResourceId instanceId;
	ResourceId sourceObjectId;
};

Result<std::optional<PrefabOverrideTarget>>
FindPrefabOverrideTarget(SceneDraft* draft, Scene* scene, const GameObject::Ptr& owner, const Component* component)
{
	if (!draft || !scene || !draft->Linked() || !draft->LinkedScene())
	{
		return Result<std::optional<PrefabOverrideTarget>>::Success(std::nullopt);
	}
	Result<ResourceId> sceneId = component ? scene->ComponentPersistentId(component->Id()) : scene->PersistentId(owner);
	if (!sceneId)
	{
		return Result<std::optional<PrefabOverrideTarget>>::Failure(sceneId.error);
	}
	const String sceneIdText = sceneId.value.ToString();
	for (const auto& instance : draft->LinkedScene()->scopedIds)
	{
		for (const auto& mapping : instance.second)
		{
			if (mapping.second != sceneIdText)
			{
				continue;
			}
			auto instanceId = ResourceId::Parse(instance.first);
			auto sourceId = ResourceId::Parse(mapping.first);
			if (!instanceId || !sourceId)
			{
				return Result<std::optional<PrefabOverrideTarget>>::Failure(!instanceId ? instanceId.error
				                                                                        : sourceId.error);
			}
			return Result<std::optional<PrefabOverrideTarget>>::Success(
			    PrefabOverrideTarget{instanceId.value, sourceId.value});
		}
	}
	return Result<std::optional<PrefabOverrideTarget>>::Success(std::nullopt);
}

Result<GroupCommitObserver> MakePrefabOverrideCommit(SceneDraft* draft, Scene* scene,
                                                     const Vector<GameObject::Ptr>& objects,
                                                     const Vector<Component*>* components = nullptr)
{
	if (!draft || !scene || !draft->Linked() || !draft->LinkedScene())
	{
		return Result<GroupCommitObserver>::Success({});
	}
	Vector<std::optional<PrefabOverrideTarget>> targets;
	if (components)
	{
		for (Component* component : *components)
		{
			auto target = FindPrefabOverrideTarget(draft, scene, component->GetGameObject(), component);
			if (!target)
			{
				return Result<GroupCommitObserver>::Failure(target.error);
			}
			targets.push_back(std::move(target.value));
		}
	}
	else
	{
		for (const auto& object : objects)
		{
			auto target = FindPrefabOverrideTarget(draft, scene, object, nullptr);
			if (!target)
			{
				return Result<GroupCommitObserver>::Failure(target.error);
			}
			targets.push_back(std::move(target.value));
		}
	}
	return Result<GroupCommitObserver>::Success(
	    [draft, targets = std::move(targets),
	     isComponent = components != nullptr](const SerializedObjectGroup& group, const String& editedPath,
	                                          const Vector<bool>& changedTargets) -> Result<void>
	    {
		    if (targets.size() != group.TargetCount() || changedTargets.size() != group.TargetCount())
		    {
			    return Result<void>::Failure("Prefab override target mapping no longer matches the selection");
		    }
		    String rootPath = editedPath;
		    const Size separator = rootPath.find('/', String("/Fields/").size());
		    if (separator != String::npos)
		    {
			    rootPath.resize(separator);
		    }
		    if (rootPath.rfind("/Fields/", 0) != 0)
		    {
			    return Result<void>::Failure("Cannot map edited property to a Prefab serialized field");
		    }
		    const String field = rootPath.substr(String("/Fields/").size());
		    String overridePath;
		    if (isComponent)
		    {
			    overridePath = "/data/" + field;
		    }
		    else if (field == "name")
		    {
			    overridePath = "/name";
		    }
		    else if (field == "active")
		    {
			    overridePath = "/enabled";
		    }
		    else if (field == "position" || field == "rotation" || field == "scale")
		    {
			    overridePath = "/localTransform/" + field;
		    }
		    else
		    {
			    return Result<void>::Failure("Unsupported Prefab override field " + field);
		    }
		    Vector<SceneInstancePropertyOverride> overrides;
		    for (Size index = 0; index < targets.size(); ++index)
		    {
			    if (!targets[index] || !changedTargets[index])
			    {
				    continue;
			    }
			    auto value = group.TargetValue(index, rootPath);
			    if (!value)
			    {
				    return Result<void>::Failure(value.error);
			    }
			    overrides.push_back(
			        {targets[index]->instanceId, targets[index]->sourceObjectId, overridePath, std::move(value.value)});
		    }
		    return draft->SetInstanceProperties(overrides);
	    });
}

Result<SerializedJson> CaptureGameObjectProperties(const GameObject::Ptr& object)
{
	if (!object)
	{
		return Result<SerializedJson>::Failure("Cannot capture an empty GameObject");
	}
	auto name = object->Name();
	auto active = object->ActiveSelf();
	auto transform = object->GetLocalTransform();
	if (!name || !active || !transform)
	{
		return Result<SerializedJson>::Failure(!name ? name.error : !active ? active.error : transform.error);
	}
	const auto& value = transform.value;
	return Result<SerializedJson>::Success(
	    {{"Fields",
	      {{"name", ToStdString(name.value)},
	       {"active", active.value},
	       {"position", {value.position.x, value.position.y, value.position.z}},
	       {"rotation", {value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w}},
	       {"scale", {value.scale.x, value.scale.y, value.scale.z}}}},
	     {"FieldTypes",
	      {{"name", {{"Kind", "String"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.String"}}},
	       {"active", {{"Kind", "Boolean"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.Boolean"}}},
	       {"position", {{"Kind", "Vector3"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.Numerics.Vector3"}}},
	       {"rotation",
	        {{"Kind", "Quaternion"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.Numerics.Quaternion"}}},
	       {"scale",
	        {{"Kind", "Vector3"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.Numerics.Vector3"}}}}}});
}

Result<SerializedJson> ApplyGameObjectProperties(const GameObject::Ptr& object, const SerializedJson& payload)
{
	if (!object || !payload.is_object() || !payload.contains("Fields") || !payload["Fields"].is_object())
	{
		return Result<SerializedJson>::Failure("Invalid prepared GameObject property payload");
	}
	try
	{
		const auto& fields = payload.at("Fields");
		if (!fields.at("name").is_string() || !fields.at("active").is_boolean() || !fields.at("position").is_array() ||
		    fields.at("position").size() != 3 || !fields.at("rotation").is_array() ||
		    fields.at("rotation").size() != 4 || !fields.at("scale").is_array() || fields.at("scale").size() != 3)
		{
			return Result<SerializedJson>::Failure("Prepared GameObject properties have an invalid shape");
		}
		LocalTransform transform;
		transform.position = {fields["position"][0].get<Float32>(), fields["position"][1].get<Float32>(),
		                      fields["position"][2].get<Float32>()};
		transform.rotation = {fields["rotation"][0].get<Float32>(), fields["rotation"][1].get<Float32>(),
		                      fields["rotation"][2].get<Float32>(), fields["rotation"][3].get<Float32>()};
		transform.scale = {fields["scale"][0].get<Float32>(), fields["scale"][1].get<Float32>(),
		                   fields["scale"][2].get<Float32>()};
		auto renamed = object->SetName(ToUrsaString(fields["name"].get<std::string>()));
		if (!renamed)
		{
			return Result<SerializedJson>::Failure(renamed.error);
		}
		auto activated = object->SetActive(fields["active"].get<bool>());
		if (!activated)
		{
			return Result<SerializedJson>::Failure(activated.error);
		}
		auto transformed = object->SetLocalTransform(transform);
		if (!transformed)
		{
			return Result<SerializedJson>::Failure(transformed.error);
		}
		return CaptureGameObjectProperties(object);
	}
	catch (const std::exception& error)
	{
		return Result<SerializedJson>::Failure(ToUrsaString(error.what()));
	}
}

Result<std::unique_ptr<SerializedObject>> CreateGameObjectSerializedObject(GameObject::Ptr object)
{
	SerializedTargetAdapter adapter;
	adapter.capture = [object] { return CaptureGameObjectProperties(object); };
	adapter.apply = [object](const SerializedJson& candidate) { return ApplyGameObjectProperties(object, candidate); };
	adapter.prepare = [](const SerializedJson& original, const SerializedJson& candidate)
	{
		SerializedTargetCheckpoint checkpoint;
		checkpoint.serializedPayload = original;
		checkpoint.backendToken = {{"candidate", candidate}};
		return Result<SerializedTargetCheckpoint>::Success(std::move(checkpoint));
	};
	adapter.validate = [](const SerializedTargetCheckpoint& checkpoint)
	{
		const auto& fields = checkpoint.backendToken.at("candidate").at("Fields");
		return fields.at("name").get<std::string>().size() <= 256
		           ? Result<void>{}
		           : Result<void>::Failure("GameObject name exceeds 256 bytes");
	};
	adapter.publish = [object](const SerializedTargetCheckpoint& checkpoint)
	{ return ApplyGameObjectProperties(object, checkpoint.backendToken.at("candidate")); };
	adapter.restore = [object](const SerializedTargetCheckpoint& checkpoint)
	{
		return ApplyGameObjectProperties(object, checkpoint.serializedPayload)
		           ? Result<void>{}
		           : Result<void>::Failure("Cannot restore GameObject checkpoint");
	};
	return SerializedObject::Create(std::move(adapter));
}

Result<SerializedObjectGroup> CreateGameObjectGroup(const Vector<GameObject::Ptr>& objects,
                                                    const GameObject::Ptr& primary,
                                                    Vector<std::unique_ptr<SerializedObject>>& serializedTargets)
{
	Vector<SerializedObject*> targets;
	Size primaryIndex = 0;
	for (const auto& object : objects)
	{
		if (object == primary)
		{
			primaryIndex = targets.size();
		}
		auto target = CreateGameObjectSerializedObject(object);
		if (!target)
		{
			return Result<SerializedObjectGroup>::Failure(target.error);
		}
		targets.push_back(target.value.get());
		serializedTargets.push_back(std::move(target.value));
	}
	return SerializedObjectGroup::Create(std::move(targets), primaryIndex);
}

void CollectPrefabGameObjects(const GameObject::Ptr& root, const Vector<ResourceId>& selectedIds,
                              Vector<GameObject::Ptr>& objects)
{
	if (!root)
	{
		return;
	}
	if (std::find(selectedIds.begin(), selectedIds.end(), root->Id()) != selectedIds.end())
	{
		objects.push_back(root);
	}
	for (const auto& child : root->Children())
	{
		CollectPrefabGameObjects(child, selectedIds, objects);
	}
}

Result<Vector<GameObject::Ptr>> SelectedSceneObjects(Scene& scene, const GameObject::Ptr& primary,
                                                     const EditorSelectionContext* selection, ResourceId documentId)
{
	Vector<GameObject::Ptr> objects;
	if (!selection || selection->entries.size() < 2)
	{
		objects.push_back(primary);
		return Result<Vector<GameObject::Ptr>>::Success(std::move(objects));
	}
	for (const auto& entry : selection->entries)
	{
		if (entry.kind != EditorSelectionKind::GameObject || entry.documentId != documentId || entry.playSession ||
		    entry.IsMissing())
		{
			continue;
		}
		auto object = scene.GameObjectById(entry.objectId);
		if (!object)
		{
			continue;
		}
		objects.push_back(object.value);
	}
	if (objects.empty())
	{
		objects.push_back(primary);
	}
	return Result<Vector<GameObject::Ptr>>::Success(std::move(objects));
}

Result<bool> DrawGameObjectGroupProperties(SerializedObjectGroup& group, const String& primaryName, String& diagnostic,
                                           const GroupCommitObserver& commitObserver = {})
{
	if (group.TargetCount() > 1)
	{
		ImGui::Text("%zu objects selected · %s is primary", group.TargetCount(), primaryName.c_str());
		ImGui::TextDisabled("Transform Gizmo edits the primary selection only.");
	}
	bool changed = false;
	const auto apply = [&](const String& path) -> Result<void>
	{
		const auto changedTargets = group.PendingTargetChanges();
		auto applied = group.Apply();
		if (!applied)
		{
			return applied;
		}
		return commitObserver ? commitObserver(group, path, changedTargets) : Result<void>{};
	};
	const auto name = group.DescribeProperty("/Fields/name");
	if (!name)
	{
		return Result<bool>::Failure(name.error);
	}
	String nameValue = ToUrsaString(name.value.at("Value").get<std::string>());
	char nameBuffer[257]{};
	std::memcpy(nameBuffer, nameValue.data(), std::min<Ursa::Size>(nameValue.size(), sizeof(nameBuffer) - 1));
	ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, name.value.value("HasMultipleDifferentValues", false));
	const bool nameChanged = ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));
	ImGui::PopItemFlag();
	if (nameChanged)
	{
		auto set = group.SetValue("/Fields/name", ToStdString(String(nameBuffer)));
		auto applied = set ? apply(ToUrsaString("/Fields/name")) : Result<void>::Failure(set.error);
		if (!applied)
		{
			diagnostic = applied.error;
			return Result<bool>::Failure(diagnostic);
		}
		changed = true;
	}
	const auto active = group.DescribeProperty("/Fields/active");
	if (!active)
	{
		return Result<bool>::Failure(active.error);
	}
	bool activeValue = active.value.at("Value").get<bool>();
	ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, active.value.value("HasMultipleDifferentValues", false));
	const bool activeChanged = ImGui::Checkbox("Active", &activeValue);
	ImGui::PopItemFlag();
	if (activeChanged)
	{
		auto set = group.SetValue("/Fields/active", activeValue);
		auto applied = set ? apply(ToUrsaString("/Fields/active")) : Result<void>::Failure(set.error);
		if (!applied)
		{
			diagnostic = applied.error;
			return Result<bool>::Failure(diagnostic);
		}
		changed = true;
	}
	Icons::Section(Icons::Icon::Move, "Transform");
	const auto drawVector = [&](const char* label, const char* path, int componentCount, float speed, float minimum,
	                            float maximum) -> Result<bool>
	{
		auto state = group.DescribeProperty(path);
		if (!state)
		{
			return Result<bool>::Failure(state.error);
		}
		const auto& current = state.value.at("Value");
		Float32 values[4]{};
		for (int index = 0; index < componentCount; ++index)
		{
			values[index] = current.at(index).get<Float32>();
		}
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, state.value.value("HasMultipleDifferentValues", false));
		const bool edited = componentCount == 3 ? ImGui::DragFloat3(label, values, speed, minimum, maximum)
		                                        : ImGui::DragFloat4(label, values, speed, minimum, maximum);
		ImGui::PopItemFlag();
		if (!edited)
		{
			return Result<bool>::Success(false);
		}
		for (int index = 0; index < componentCount; ++index)
		{
			if (values[index] == current.at(index).get<Float32>())
			{
				continue;
			}
			const String componentPath = String(path) + "/" + ToUrsaString(std::to_string(index));
			auto set = group.SetValue(componentPath, values[index]);
			if (!set)
			{
				return Result<bool>::Failure(set.error);
			}
		}
		auto applied = apply(ToUrsaString(path));
		return applied ? Result<bool>::Success(true) : Result<bool>::Failure(applied.error);
	};
	auto position = drawVector("Position", "/Fields/position", 3, 0.05f, 0.0f, 0.0f);
	if (!position)
	{
		return position;
	}
	changed |= position.value;
	auto rotation = drawVector("Rotation", "/Fields/rotation", 4, 0.01f, 0.0f, 0.0f);
	if (!rotation)
	{
		return rotation;
	}
	changed |= rotation.value;
	auto scale = drawVector("Scale", "/Fields/scale", 3, 0.05f, 0.001f, 100.0f);
	if (!scale)
	{
		return scale;
	}
	changed |= scale.value;
	diagnostic.clear();
	return Result<bool>::Success(changed);
}

Result<bool> SetAndApplyGroupValue(SerializedObjectGroup& group, const String& path, SerializedJson value,
                                   const GroupCommitObserver& commitObserver = {})
{
	auto set = group.SetValue(path, std::move(value));
	if (!set)
	{
		return Result<bool>::Failure(set.error);
	}
	const auto changedTargets = group.PendingTargetChanges();
	auto applied = group.Apply();
	if (!applied)
	{
		return Result<bool>::Failure(applied.error);
	}
	if (commitObserver)
	{
		auto observed = commitObserver(group, path, changedTargets);
		if (!observed)
		{
			return Result<bool>::Failure(observed.error);
		}
	}
	return Result<bool>::Success(
	    std::any_of(changedTargets.begin(), changedTargets.end(), [](bool changed) { return changed; }));
}

Result<bool> ApplyGroupCandidate(SerializedObjectGroup& group, const String& path, const SerializedJson& candidate,
                                 const GroupCommitObserver& commitObserver,
                                 const SerializedJson* originalValues = nullptr)
{
	auto set = group.SetValue(path, candidate);
	if (!set)
	{
		return Result<bool>::Failure(set.error);
	}
	Vector<bool> changedTargets = group.PendingTargetChanges();
	if (originalValues && originalValues->is_array() && originalValues->size() == group.TargetCount())
	{
		auto compared = group.CompareTargetValues(path, *originalValues, candidate);
		if (!compared)
		{
			return Result<bool>::Failure(compared.error);
		}
		changedTargets = std::move(compared.value);
	}
	auto applied = group.Apply();
	if (!applied)
	{
		return Result<bool>::Failure(applied.error);
	}
	const bool changed =
	    std::any_of(changedTargets.begin(), changedTargets.end(), [](bool targetChanged) { return targetChanged; });
	if (!changed)
	{
		return Result<bool>::Success(false);
	}
	if (commitObserver)
	{
		auto observed = commitObserver(group, path, changedTargets);
		if (!observed)
		{
			return Result<bool>::Failure(observed.error);
		}
	}
	return Result<bool>::Success(true);
}

Result<void> PreviewGroupCandidate(SerializedObjectGroup& group, const String& path, const SerializedJson& candidate)
{
	auto set = group.SetValue(path, candidate);
	return set ? group.Apply() : set;
}

Result<bool> DrawGroupFloat(SerializedObjectGroup& group, const char* label, const char* path, Float32 multiplier,
                            Float32 minimum, Float32 maximum, Float32 speed, const GroupCommitObserver& commitObserver)
{
	auto state = group.DescribeProperty(ToUrsaString(path));
	if (!state)
	{
		return Result<bool>::Failure(state.error);
	}
	Float32 value = state.value.at("Value").get<Float32>() * multiplier;
	ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, state.value.value("HasMultipleDifferentValues", false));
	const bool edited = ImGui::DragFloat(label, &value, speed, minimum, maximum);
	ImGui::PopItemFlag();
	if (!edited)
	{
		return Result<bool>::Success(false);
	}
	return SetAndApplyGroupValue(group, ToUrsaString(path), Float64(value / multiplier), commitObserver);
}

Result<bool> DrawGroupBool(SerializedObjectGroup& group, const char* label, const char* path,
                           const GroupCommitObserver& commitObserver)
{
	auto state = group.DescribeProperty(ToUrsaString(path));
	if (!state)
	{
		return Result<bool>::Failure(state.error);
	}
	bool value = state.value.at("Value").get<bool>();
	ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, state.value.value("HasMultipleDifferentValues", false));
	const bool edited = ImGui::Checkbox(label, &value);
	ImGui::PopItemFlag();
	return edited ? SetAndApplyGroupValue(group, ToUrsaString(path), value, commitObserver)
	              : Result<bool>::Success(false);
}

Result<bool> DrawGroupColor(SerializedObjectGroup& group, const GroupCommitObserver& commitObserver)
{
	const String path = ToUrsaString("/Fields/color");
	auto state = group.DescribeProperty(path);
	if (!state)
	{
		return Result<bool>::Failure(state.error);
	}
	if (!state.value.at("Value").is_array() || state.value.at("Value").size() != 4)
	{
		return Result<bool>::Failure("Light color property does not contain four channels");
	}
	Float32 value[4]{};
	for (int channel = 0; channel < 4; ++channel)
	{
		value[channel] = state.value.at("Value").at(channel).get<Float32>();
	}
	ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, state.value.value("HasMultipleDifferentValues", false));
	const bool edited = ImGui::ColorEdit4("Color", value);
	ImGui::PopItemFlag();
	return edited ? SetAndApplyGroupValue(group, path, {value[0], value[1], value[2], value[3]}, commitObserver)
	              : Result<bool>::Success(false);
}

Result<bool> DrawDefaultScriptComponentGroup(const ComponentTargetGroup& componentGroup, Size primaryIndex,
                                             const ComponentRegistry& registry, ComponentInspectorContext& context,
                                             ComponentLoadContext& loadContext, ProjectWorkspace& workspace,
                                             const Vector<ScriptTypeDescriptor>* scriptTypes, Scene* scene,
                                             SceneDraft* draft)
{
	Vector<std::unique_ptr<SerializedObject>> serializedTargets;
	Vector<SerializedObject*> properties;
	ComponentSaveContext saveContext;
	saveContext.scene = scene;
	saveContext.serialization = &loadContext;
	for (Component* component : componentGroup.targets)
	{
		auto serialized = SerializedObject::Create(*component, registry, loadContext, saveContext);
		if (!serialized)
		{
			return Result<bool>::Failure(serialized.error);
		}
		properties.push_back(serialized.value.get());
		serializedTargets.push_back(std::move(serialized.value));
	}
	auto created = SerializedObjectGroup::Create(std::move(properties), primaryIndex);
	if (!created)
	{
		return Result<bool>::Failure(created.error);
	}
	auto& group = created.value;
	auto commitObserver = MakePrefabOverrideCommit(draft, scene, {}, &componentGroup.targets);
	if (!commitObserver)
	{
		return Result<bool>::Failure(commitObserver.error);
	}
	context.targets = componentGroup.targets;
	context.primaryIndex = primaryIndex;
	const auto title = componentGroup.stableType.substr(String("script:").size()) + " [" +
	                   ToUrsaString(std::to_string(componentGroup.sameTypeIndex + 1)) + "]";
	ImGui::SeparatorText(title.c_str());
	ImGui::TextDisabled("Common serialized fields across %zu objects", group.TargetCount());
	bool changed = false;
	for (const String& path : group.CommonPropertyPaths())
	{
		if (path.rfind("/Fields/", 0) != 0 || path.find('/', String("/Fields/").size()) != String::npos)
		{
			continue;
		}
		auto described = group.DescribeProperty(path);
		if (!described)
		{
			continue;
		}
		const String kind = ToUrsaString(described.value.value("Kind", std::string("Unsupported")));
		if (kind == "Unsupported")
		{
			continue;
		}
		String fieldName = path.substr(String("/Fields/").size());
		for (Size offset = 0; (offset = fieldName.find("~1", offset)) != String::npos;)
		{
			fieldName.replace(offset, 2, "/");
			++offset;
		}
		for (Size offset = 0; (offset = fieldName.find("~0", offset)) != String::npos;)
		{
			fieldName.replace(offset, 2, "~");
			++offset;
		}
		SerializedJson candidate = described.value.at("Value");
		String elementKind = ToUrsaString(described.value.value("ElementKind", std::string("Unsupported")));
		ImGui::PushID(fieldName.c_str());
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(fieldName.c_str());
		ImGui::SameLine(150.0f);
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, described.value.value("HasMultipleDifferentValues", false));
		ReferencePickerTransaction pickerTransaction;
		pickerTransaction.capture = [&group, path] { return group.TargetValues(path); };
		pickerTransaction.previewCandidate = [&](const SerializedJson& candidateValue)
		{ return PreviewGroupCandidate(group, path, candidateValue); };
		pickerTransaction.applyCandidate = [&](const SerializedJson& candidateValue, const SerializedJson& checkpoint)
		{ return ApplyGroupCandidate(group, path, candidateValue, commitObserver.value, &checkpoint); };
		pickerTransaction.appendReferences = [&](const Vector<SerializedJson>& references) -> Result<bool>
		{
			auto appended = group.AppendReferences(path, references);
			if (!appended)
			{
				return Result<bool>::Failure(appended.error);
			}
			const auto targetChanges = group.PendingTargetChanges();
			auto applied = group.Apply();
			if (!applied)
			{
				return Result<bool>::Failure(applied.error);
			}
			if (commitObserver && commitObserver.value)
			{
				auto observed = commitObserver.value(group, path, targetChanges);
				if (!observed)
				{
					return Result<bool>::Failure(observed.error);
				}
			}
			return Result<bool>::Success(
			    std::any_of(targetChanges.begin(), targetChanges.end(), [](bool changed) { return changed; }));
		};
		pickerTransaction.restore = [&group, path](const SerializedJson& checkpoint)
		{ return group.RestoreTargetValues(path, checkpoint); };
		bool transactionHandled = false;
		const bool edited = DrawSerializedScriptValue(
		    candidate, kind, elementKind, described.value, workspace, context, scriptTypes,
		    (kind == "AssetReference" || kind == "ScriptableObjectReference" || kind == "MeshReference" ||
		     kind == "MaterialReference" || kind == "GameObjectReference" || kind == "ComponentReference")
		        ? &pickerTransaction
		        : nullptr,
		    &transactionHandled);
		ImGui::PopItemFlag();
		ImGui::PopID();
		if (transactionHandled)
		{
			changed |= edited;
			continue;
		}
		if (edited)
		{
			auto set = group.SetValue(path, std::move(candidate));
			if (!set)
			{
				return Result<bool>::Failure(set.error);
			}
			const auto targetChanges = group.PendingTargetChanges();
			auto applied = group.Apply();
			if (!applied)
			{
				return Result<bool>::Failure(applied.error);
			}
			auto observed = commitObserver.value(group, path, targetChanges);
			if (!observed)
			{
				return Result<bool>::Failure(observed.error);
			}
			changed |= std::any_of(targetChanges.begin(), targetChanges.end(),
			                       [](bool targetChanged) { return targetChanged; });
		}
	}
	return Result<bool>::Success(changed);
}

Result<bool> DrawNativeComponentGroup(const ComponentTargetGroup& componentGroup, Size primaryIndex,
                                      const ComponentRegistry& registry, ComponentEditors& componentEditors,
                                      ComponentInspectorContext& inspectorContext, ComponentLoadContext& loadContext,
                                      ProjectWorkspace& workspace, const Vector<ScriptTypeDescriptor>* scriptTypes,
                                      Scene* scene, SceneDraft* draft)
{
	if (componentGroup.stableType.rfind("script:", 0) == 0)
	{
		const String type = componentGroup.stableType.substr(String("script:").size());
		ImGui::SeparatorText(type.c_str());
		if (!componentEditors.SupportsMultipleObjects(type))
		{
			return DrawDefaultScriptComponentGroup(componentGroup, primaryIndex, registry, inspectorContext,
			                                       loadContext, workspace, scriptTypes, scene, draft);
		}
		const auto* editor = componentEditors.Find(type);
		if (!editor || primaryIndex >= componentGroup.targets.size())
		{
			return Result<bool>::Failure("The registered multi-object Inspector target group is unavailable");
		}
		inspectorContext.targets = componentGroup.targets;
		inspectorContext.primaryIndex = primaryIndex;
		ImGuiInspectorUI ui;
		auto edited = (*editor)(*componentGroup.targets[primaryIndex], inspectorContext, ui);
		if (!edited)
		{
			return Result<bool>::Failure(edited.error);
		}
		return Result<bool>::Success(edited.value);
	}
	const bool supported = componentGroup.stableType == "native:ursa.Camera" ||
	                       componentGroup.stableType == "native:ursa.DirectionalLight" ||
	                       componentGroup.stableType == "native:ursa.PointLight" ||
	                       componentGroup.stableType == "native:ursa.MeshFilter" ||
	                       componentGroup.stableType == "native:ursa.MeshRenderer";
	if (!supported)
	{
		ImGui::SeparatorText(componentGroup.stableType.c_str());
		ImGui::TextDisabled("No aggregate editor is registered for this native component.");
		return Result<bool>::Success(false);
	}
	Vector<std::unique_ptr<SerializedObject>> serializedTargets;
	Vector<SerializedObject*> properties;
	ComponentSaveContext saveContext;
	saveContext.scene = scene;
	saveContext.serialization = &loadContext;
	for (Component* component : componentGroup.targets)
	{
		auto serialized = SerializedObject::Create(*component, registry, loadContext, saveContext);
		if (!serialized)
		{
			return Result<bool>::Failure(serialized.error);
		}
		properties.push_back(serialized.value.get());
		serializedTargets.push_back(std::move(serialized.value));
	}
	auto groupResult = SerializedObjectGroup::Create(std::move(properties), primaryIndex);
	if (!groupResult)
	{
		return Result<bool>::Failure(groupResult.error);
	}
	auto& group = groupResult.value;
	auto overrideCommit = MakePrefabOverrideCommit(draft, scene, {}, &componentGroup.targets);
	if (!overrideCommit)
	{
		return Result<bool>::Failure(overrideCommit.error);
	}
	const String title =
	    componentGroup.stableType + " [" + ToUrsaString(std::to_string(componentGroup.sameTypeIndex + 1)) + "]";
	ImGui::SeparatorText(title.c_str());
	ImGui::TextDisabled("%zu matching components", group.TargetCount());
	bool changed = false;
	const auto drawFloat = [&](const char* label, const char* path, Float32 multiplier, Float32 minimum,
	                           Float32 maximum, Float32 speed) -> Result<void>
	{
		auto drawn = DrawGroupFloat(group, label, path, multiplier, minimum, maximum, speed, overrideCommit.value);
		if (!drawn)
		{
			return Result<void>::Failure(drawn.error);
		}
		changed |= drawn.value;
		return {};
	};
	const auto drawBool = [&](const char* label, const char* path) -> Result<void>
	{
		auto drawn = DrawGroupBool(group, label, path, overrideCommit.value);
		if (!drawn)
		{
			return Result<void>::Failure(drawn.error);
		}
		changed |= drawn.value;
		return {};
	};
	if (componentGroup.stableType == "native:ursa.Camera")
	{
		auto result = drawFloat("FOV degrees", "/Fields/verticalFovRadians", 57.2957795f, 1.f, 179.f, 0.5f);
		if (result)
		{
			result = drawBool("Orthographic", "/Fields/orthographic");
		}
		if (result)
		{
			result = drawFloat("Orthographic size", "/Fields/orthographicSize", 1.f, 0.001f, 100000.f, 0.05f);
		}
		if (result)
		{
			result = drawFloat("Near", "/Fields/nearPlane", 1.f, 0.001f, 10000.f, 0.01f);
		}
		if (result)
		{
			result = drawFloat("Far", "/Fields/farPlane", 1.f, 0.002f, 100000.f, 0.1f);
		}
		if (!result)
		{
			return Result<bool>::Failure(result.error);
		}
	}
	else if (componentGroup.stableType == "native:ursa.DirectionalLight")
	{
		auto color = DrawGroupColor(group, overrideCommit.value);
		if (!color)
		{
			return color;
		}
		changed |= color.value;
		for (const auto& field : Vector<std::tuple<const char*, const char*, Float32, Float32, Float32>>{
		         {"Intensity", "/Fields/intensity", 0.f, 10000.f, 0.05f},
		         {"Shadow strength", "/Fields/shadowStrength", 0.f, 1.f, 0.01f},
		         {"Shadow bias", "/Fields/shadowBias", 0.f, 1.f, 0.0001f},
		         {"Shadow normal bias", "/Fields/shadowNormalBias", 0.f, 10.f, 0.001f}})
		{
			auto result = drawFloat(std::get<0>(field), std::get<1>(field), 1.f, std::get<2>(field), std::get<3>(field),
			                        std::get<4>(field));
			if (!result)
			{
				return Result<bool>::Failure(result.error);
			}
		}
		auto casts = drawBool("Casts shadows", "/Fields/castsShadows");
		auto soft = casts ? drawBool("Soft shadows", "/Fields/softShadows") : casts;
		if (!soft)
		{
			return Result<bool>::Failure(soft.error);
		}
	}
	else if (componentGroup.stableType == "native:ursa.PointLight")
	{
		auto color = DrawGroupColor(group, overrideCommit.value);
		if (!color)
		{
			return color;
		}
		changed |= color.value;
		auto intensity = drawFloat("Intensity", "/Fields/intensity", 1.f, 0.f, 10000.f, 0.05f);
		auto range = intensity ? drawFloat("Range", "/Fields/range", 1.f, 0.001f, 10000.f, 0.05f) : intensity;
		if (!range)
		{
			return Result<bool>::Failure(range.error);
		}
	}
	else if (componentGroup.stableType == "native:ursa.MeshRenderer")
	{
		auto cast = drawBool("Cast shadows", "/Fields/castShadows");
		auto receive = cast ? drawBool("Receive shadows", "/Fields/receiveShadows") : cast;
		if (!receive)
		{
			return Result<bool>::Failure(receive.error);
		}
		const String materialsPath = ToUrsaString("/Fields/materials");
		auto materials = group.DescribeProperty(materialsPath);
		if (!materials)
		{
			return Result<bool>::Failure(materials.error);
		}
		ImGui::SeparatorText("Materials");
		int slotCount = static_cast<int>(materials.value.value("PrimaryArraySize", 0));
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, materials.value.value("HasMultipleDifferentArraySizes", false));
		const bool resize = ImGui::InputInt("Slot count", &slotCount);
		ImGui::PopItemFlag();
		if (resize)
		{
			slotCount = std::clamp(slotCount, 0, static_cast<int>(Ursa::Mesh::maxSubMeshes));
			auto resized = group.ResizeCollection(materialsPath, static_cast<Size>(slotCount));
			if (!resized)
			{
				return Result<bool>::Failure(resized.error);
			}
			const auto changedTargets = group.PendingTargetChanges();
			auto applied = group.Apply();
			if (!applied)
			{
				return Result<bool>::Failure(applied.error);
			}
			if (overrideCommit.value)
			{
				auto observed = overrideCommit.value(group, materialsPath, changedTargets);
				if (!observed)
				{
					return Result<bool>::Failure(observed.error);
				}
			}
			changed = true;
			materials = group.DescribeProperty(materialsPath);
			if (!materials)
			{
				return Result<bool>::Failure(materials.error);
			}
		}
		const Size commonSlots = materials.value.value("ArraySize", Size(0));
		for (Size index = 0; index < commonSlots; ++index)
		{
			const String slotPath = materialsPath + "/" + ToUrsaString(std::to_string(index));
			auto slot = group.DescribeProperty(slotPath);
			if (!slot)
			{
				continue;
			}
			const SerializedJson binding = slot.value.at("Value").value("value", SerializedJson(nullptr));
			const bool mixed = slot.value.value("HasMultipleDifferentValues", false);
			const char* label = mixed ? "Mixed" : binding.is_null() ? "None" : "Assigned";
			ImGui::Text("Slot %zu: %s", index, label);
			ImGui::SameLine();
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::SmallButton("Clear"))
			{
				auto cleared = SetAndApplyGroupValue(group, slotPath, {{"value", nullptr}, {"instantiated", false}},
				                                     overrideCommit.value);
				if (!cleared)
				{
					return Result<bool>::Failure(cleared.error);
				}
				changed |= cleared.value;
			}
			ImGui::PopID();
		}
		ImGui::TextDisabled("Material selection opens in the object picker when available.");
	}
	else if (componentGroup.stableType == "native:ursa.MeshFilter")
	{
		auto mesh = group.DescribeProperty(ToUrsaString("/Fields/mesh"));
		if (!mesh)
		{
			return Result<bool>::Failure(mesh.error);
		}
		const bool mixed = mesh.value.value("HasMultipleDifferentValues", false);
		const char* meshLabel = mixed ? "Mesh: --" : mesh.value.at("Value").is_null() ? "Mesh: None" : "Mesh: Assigned";
		ImGui::TextDisabled("%s", meshLabel);
		if (ImGui::Button("Clear mesh binding"))
		{
			auto applied = SetAndApplyGroupValue(group, ToUrsaString("/Fields/mesh"), nullptr, overrideCommit.value);
			if (!applied)
			{
				return Result<bool>::Failure(applied.error);
			}
			changed |= applied.value;
		}
	}
	else
	{
		ImGui::TextDisabled("No aggregate editor is registered for this native component.");
	}
	return Result<bool>::Success(changed);
}

Result<bool> DrawCommonComponentSummary(const Vector<GameObject::Ptr>& selectedObjects,
                                        const GameObject::Ptr& primaryObject, const ComponentRegistry& registry,
                                        ComponentEditors& componentEditors, ComponentInspectorContext& inspectorContext,
                                        ComponentLoadContext& loadContext, ProjectWorkspace& workspace,
                                        const Vector<ScriptTypeDescriptor>* scriptTypes, Scene* scene,
                                        SceneDraft* draft)
{
	const auto primary = std::find(selectedObjects.begin(), selectedObjects.end(), primaryObject);
	if (primary == selectedObjects.end())
	{
		return Result<bool>::Failure("Primary object is missing from the component selection");
	}
	const Size primaryIndex = static_cast<Size>(std::distance(selectedObjects.begin(), primary));
	auto mapping = BuildCommonComponentMapping(selectedObjects, primaryIndex);
	if (!mapping)
	{
		return Result<bool>::Failure(mapping.error);
	}
	ImGui::SeparatorText("Common Components");
	ImGui::TextDisabled("Only components shared by every selected object can be edited.");
	if (mapping.value.empty())
	{
		ImGui::TextDisabled("No common components");
	}
	bool changed = false;
	for (const auto& group : mapping.value)
	{
		ImGui::PushID((group.stableType + ":" + ToUrsaString(std::to_string(group.sameTypeIndex))).c_str());
		auto edited = DrawNativeComponentGroup(group, primaryIndex, registry, componentEditors, inspectorContext,
		                                       loadContext, workspace, scriptTypes, scene, draft);
		if (!edited)
		{
			ImGui::PopID();
			return Result<bool>::Failure(edited.error);
		}
		changed |= edited.value;
		ImGui::SameLine();
		bool removeFromAll = ImGui::SmallButton("Remove from all");
		if (ImGui::BeginPopupContextItem("CommonComponentActions"))
		{
			removeFromAll |= ImGui::MenuItem("Remove from all selected objects");
			ImGui::EndPopup();
		}
		if (removeFromAll)
		{
			auto removed = RemoveCommonComponentFromSelection(selectedObjects, group);
			if (!removed)
			{
				ImGui::PopID();
				return Result<bool>::Failure(removed.error);
			}
			changed = true;
		}
		ImGui::PopID();
	}
	return Result<bool>::Success(changed);
}

bool DrawAddComponentBrowser(ComponentEditors& componentEditors, ComponentInspectorContext& context, String& diagnostic,
                             const Vector<ScriptTypeDescriptor>* scriptTypes,
                             const Vector<GameObject::Ptr>& selectedObjects)
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
				if (selectedObjects.size() > 1)
				{
					auto added = AddComponentToSelection(selectedObjects, componentEditors, descriptor.type, context);
					diagnostic = added ? String{} : added.error;
					changed = static_cast<bool>(added);
				}
				else
				{
					auto created = componentEditors.Create(descriptor.type, context);
					diagnostic = created ? String{} : created.error;
					changed = static_cast<bool>(created);
				}
				if (changed)
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

bool DrawSerializedReference(nlohmann::json& value, const String& kind, const String& expectedType,
                             const ComponentInspectorContext& context, const Vector<ScriptTypeDescriptor>* scriptTypes,
                             const ReferencePickerTransaction* transaction, bool* transactionHandled)
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
	Vector<ReferencePickerOption> options;
	options.push_back({"none", "None", "None", {}, true});
	Vector<GameObject::Ptr> pending = context.roots;
	while (!pending.empty())
	{
		GameObject::Ptr object = std::move(pending.back());
		pending.pop_back();
		if (kind == "GameObjectReference")
		{
			const ResourceId id = context.scene ? context.scene->PersistentId(object).value : object->Id();
			options.push_back({"object:" + id.ToString(), object->Name().value, "GameObject", "Scene", true});
		}
		else
		{
			const auto compatible = CompatibleComponentsForReference(*object, expectedType, scriptTypes);
			for (Component* component : object->Components())
			{
				if (std::find(compatible.begin(), compatible.end(), component) == compatible.end())
				{
					continue;
				}
				ResourceId id = component->PersistentId();
				if (context.scene)
				{
					auto persistent = context.scene->ComponentPersistentId(component->Id());
					if (persistent)
					{
						id = persistent.value;
					}
				}
				options.push_back({"component:" + id.ToString(),
				                   object->Name().value + " / " + component->SerializationType(),
				                   component->SerializationType(), "Scene", true});
			}
		}
		for (const auto& child : object->Children())
		{
			pending.push_back(child);
		}
	}
	String preview = currentId ? currentId.ToString() : String("None");
	if (currentId)
	{
		const String key = (kind == "GameObjectReference" ? "object:" : "component:") + currentId.ToString();
		const auto current =
		    std::find_if(options.begin(), options.end(), [&](const auto& option) { return option.key == key; });
		if (current != options.end())
		{
			preview = current->name;
		}
	}
	Vector<SerializedJson> candidateValues;
	candidateValues.reserve(options.size());
	candidateValues.push_back(nullptr);
	for (Size index = 1; index < options.size(); ++index)
	{
		const auto separator = options[index].key.find(':');
		const String id = options[index].key.substr(separator + 1);
		candidateValues.push_back(
		    {{"$ursaReference",
		      {{"Kind", kind == "GameObjectReference" ? "GameObject" : "Component"}, {"Id", ToStdString(id)}}}});
	}
	ReferencePickerTransaction pickerTransaction;
	const ReferencePickerTransaction* activeTransaction = nullptr;
	if (transaction)
	{
		pickerTransaction = MapPickerValues(*transaction, transactionHandled, candidateValues);
		activeTransaction = &pickerTransaction;
	}
	auto locateReference = [&context, currentId, kind]() -> Result<void>
	{
		if (!context.selection || !context.documentId)
		{
			return Result<void>::Failure("This reference has no active editor selection context");
		}
		GameObject::Ptr target;
		if (kind == "GameObjectReference")
		{
			if (context.scene)
			{
				auto found = context.scene->GameObjectById(currentId);
				if (found)
				{
					target = found.value;
				}
			}
			else
			{
				Vector<GameObject::Ptr> pending = context.roots;
				while (!pending.empty() && !target)
				{
					auto object = std::move(pending.back());
					pending.pop_back();
					if (object->Id() == currentId)
					{
						target = object;
						break;
					}
					pending.insert(pending.end(), object->Children().begin(), object->Children().end());
				}
			}
		}
		else if (context.scene)
		{
			auto componentId = context.scene->ComponentByPersistentId(currentId);
			if (componentId)
			{
				Component* component = context.scene->GetComponent(componentId.value);
				if (component)
				{
					target = component->GetGameObject();
				}
			}
		}
		if (!target)
		{
			return Result<void>::Failure("The referenced scene object no longer exists");
		}
		EditorSelectionEntry entry;
		entry.kind = EditorSelectionKind::GameObject;
		entry.documentId = context.documentId;
		entry.objectId = target->Id();
		if (!context.selection->SelectEntries({entry}, 0, entry))
		{
			return Result<void>::Failure("The editor refused to navigate to the referenced object");
		}
		return {};
	};
	SerializedJson acceptedCheckpoint;
	const auto selected = DrawReferencePickerButton(
	    "serialized-reference:" + ToUrsaString(std::to_string(ImGui::GetID("##Reference"))), preview,
	    kind == "GameObjectReference" ? "GameObject" : "Component", options,
	    currentId ? (kind == "GameObjectReference" ? "object:" : "component:") + currentId.ToString() : String{}, {},
	    activeTransaction, &acceptedCheckpoint,
	    currentId ? std::function<Result<void>()>(locateReference) : std::function<Result<void>()>{});
	auto applyDropCandidate = [&](SerializedJson candidate) -> bool
	{
		if (!transaction || !transaction->applyCandidate)
		{
			value = std::move(candidate);
			return true;
		}
		auto checkpoint = transaction->capture
		                      ? transaction->capture()
		                      : Result<SerializedJson>::Failure("Reference field has no transaction checkpoint");
		if (!checkpoint)
		{
			ImGui::TextWrapped("Cannot assign reference: %s", checkpoint.error.c_str());
			return false;
		}
		auto applied = transaction->applyCandidate(candidate, checkpoint.value);
		if (transactionHandled)
		{
			*transactionHandled = true;
		}
		if (!applied)
		{
			ImGui::TextWrapped("Cannot assign reference: %s", applied.error.c_str());
			auto restored = RestoreReferencePickerTransaction(*transaction, transactionHandled, checkpoint.value);
			if (!restored)
			{
				ImGui::TextWrapped("Rollback failed: %s", restored.error.c_str());
			}
			return false;
		}
		return applied.value;
	};
	bool droppedChanged = false;
	static std::map<String, ResourceId> pendingComponentDrop;
	const String controlKey = "serialized-reference:" + ToUrsaString(std::to_string(ImGui::GetID("##Reference")));
	if (ImGui::BeginPopup("ChooseDroppedComponent"))
	{
		auto pendingDrop = pendingComponentDrop.find(controlKey);
		GameObject::Ptr owner;
		if (pendingDrop != pendingComponentDrop.end())
		{
			Vector<GameObject::Ptr> objects = context.roots;
			while (!objects.empty() && !owner)
			{
				auto object = std::move(objects.back());
				objects.pop_back();
				const ResourceId identity = context.scene ? context.scene->PersistentId(object).value : object->Id();
				if (identity == pendingDrop->second)
				{
					owner = object;
					break;
				}
				objects.insert(objects.end(), object->Children().begin(), object->Children().end());
			}
		}
		if (owner)
		{
			for (Component* component : CompatibleComponentsForReference(*owner, expectedType, scriptTypes))
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
				const String item = component->SerializationType() + "##" + id.ToString();
				if (ImGui::Selectable(item.c_str()))
				{
					const auto option = std::find_if(options.begin(), options.end(), [&](const auto& entry)
					                                 { return entry.key == "component:" + id.ToString(); });
					if (option != options.end())
					{
						const auto index = static_cast<Size>(std::distance(options.begin(), option));
						applyDropCandidate(candidateValues[index]);
					}
					pendingComponentDrop.erase(controlKey);
					ImGui::CloseCurrentPopup();
				}
			}
		}
		else
		{
			ImGui::TextDisabled("The dropped object is no longer available.");
		}
		if (ImGui::Button("Cancel"))
		{
			pendingComponentDrop.erase(controlKey);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const auto* payload = AcceptEditorDragDropPayload())
		{
			auto decoded = DecodeEditorDragPayload(payload->DataType, payload->Data, payload->DataSize);
			if (decoded)
			{
				if (kind == "GameObjectReference")
				{
					auto dragged = SingleGameObjectDragReference(decoded.value);
					if (dragged && context.documentId && dragged.value.documentId == context.documentId)
					{
						const String key = "object:" + dragged.value.objectId.ToString();
						const auto option = std::find_if(options.begin(), options.end(),
						                                 [&](const auto& entry) { return entry.key == key; });
						if (option != options.end())
						{
							const auto index = static_cast<Size>(std::distance(options.begin(), option));
							droppedChanged = applyDropCandidate(candidateValues[index]) || droppedChanged;
						}
					}
					else if (!dragged)
					{
						ImGui::SetTooltip("This field accepts one GameObject from the current document.");
					}
				}
				else if (decoded.value.sources.size() == 1)
				{
					auto component = SingleComponentDragReference(decoded.value);
					if (component && context.documentId && component.value.owner.documentId == context.documentId)
					{
						Vector<GameObject::Ptr> objects = context.roots;
						GameObject::Ptr owner;
						while (!objects.empty() && !owner)
						{
							auto object = std::move(objects.back());
							objects.pop_back();
							if (object->Id() == component.value.owner.objectId)
							{
								owner = object;
								break;
							}
							objects.insert(objects.end(), object->Children().begin(), object->Children().end());
						}
						if (owner)
						{
							for (Component* target :
							     CompatibleComponentsForReference(*owner, expectedType, scriptTypes))
							{
								ResourceId id = target->PersistentId();
								if (context.scene)
								{
									auto persistent = context.scene->ComponentPersistentId(target->Id());
									if (persistent)
									{
										id = persistent.value;
									}
								}
								if (id == component.value.componentId)
								{
									const String key = "component:" + id.ToString();
									const auto option =
									    std::find_if(options.begin(), options.end(),
									                 [&](const auto& entry) { return entry.key == key; });
									if (option != options.end())
									{
										const auto index = static_cast<Size>(std::distance(options.begin(), option));
										droppedChanged = applyDropCandidate(candidateValues[index]) || droppedChanged;
									}
									break;
								}
							}
						}
					}
					else
					{
						auto gameObject = SingleGameObjectDragReference(decoded.value);
						if (gameObject && context.documentId && gameObject.value.documentId == context.documentId)
						{
							Vector<GameObject::Ptr> objects = context.roots;
							GameObject::Ptr owner;
							while (!objects.empty() && !owner)
							{
								auto object = std::move(objects.back());
								objects.pop_back();
								if (object->Id() == gameObject.value.objectId)
								{
									owner = object;
									break;
								}
								objects.insert(objects.end(), object->Children().begin(), object->Children().end());
							}
							if (owner)
							{
								const auto compatible =
								    CompatibleComponentsForReference(*owner, expectedType, scriptTypes);
								if (compatible.size() == 1)
								{
									ResourceId id = compatible.front()->PersistentId();
									if (context.scene)
									{
										auto persistent =
										    context.scene->ComponentPersistentId(compatible.front()->Id());
										if (persistent)
										{
											id = persistent.value;
										}
									}
									const auto option =
									    std::find_if(options.begin(), options.end(), [&](const auto& entry)
									                 { return entry.key == "component:" + id.ToString(); });
									if (option != options.end())
									{
										const auto index = static_cast<Size>(std::distance(options.begin(), option));
										droppedChanged = applyDropCandidate(candidateValues[index]) || droppedChanged;
									}
								}
								else if (compatible.size() > 1)
								{
									ResourceId ownerId =
									    context.scene ? context.scene->PersistentId(owner).value : owner->Id();
									pendingComponentDrop[controlKey] = ownerId;
									ImGui::OpenPopup("ChooseDroppedComponent");
								}
							}
						}
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (!selected)
	{
		return droppedChanged;
	}
	if (*selected == 0)
	{
		value = nullptr;
	}
	else
	{
		const auto& key = options[*selected].key;
		const auto separator = key.find(':');
		const String id = key.substr(separator + 1);
		value = {{"$ursaReference",
		          {{"Kind", kind == "GameObjectReference" ? "GameObject" : "Component"}, {"Id", ToStdString(id)}}}};
	}
	if (transaction && transaction->applyCandidate)
	{
		auto applied = transaction->applyCandidate(value, acceptedCheckpoint);
		if (transactionHandled)
		{
			*transactionHandled = true;
		}
		if (!applied)
		{
			ImGui::TextWrapped("Cannot assign reference: %s", applied.error.c_str());
			auto restored = RestoreReferencePickerTransaction(*transaction, transactionHandled, acceptedCheckpoint);
			if (!restored)
			{
				ImGui::TextWrapped("Rollback failed: %s", restored.error.c_str());
			}
			return false;
		}
		return applied.value;
	}
	return true;
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
		if (const auto* payload = AcceptEditorDragDropPayload())
		{
			auto decoded = DecodeEditorDragPayload(payload->DataType, payload->Data, payload->DataSize);
			auto dropped =
			    decoded ? SingleAssetDragReference(decoded.value) : Result<AssetReference>::Failure(decoded.error);
			if (dropped)
			{
				const auto object = std::find_if(choices.begin(), choices.end(),
				                                 [&](const auto& item)
				                                 {
					                                 return item.guid == dropped.value.guid &&
					                                        (item.localFileId == dropped.value.localFileId ||
					                                         (!dropped.value.localFileId && item.isMainObject));
				                                 });
				if (object != choices.end() && (!compatible || compatible(*object)))
				{
					reference = dropped.value;
					accepted = true;
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	return accepted;
}

ReferencePickerTransaction MapPickerValues(const ReferencePickerTransaction& transaction, bool* transactionHandled,
                                           Vector<SerializedJson> candidateValues)
{
	ReferencePickerTransaction mapped;
	mapped.capture = transaction.capture;
	mapped.preview = [&transaction, transactionHandled,
	                  candidateValues = std::move(candidateValues)](Size index) -> Result<void>
	{
		if (!transaction.previewCandidate)
		{
			return Result<void>::Failure("Reference picker transaction has no candidate preview callback");
		}
		if (index >= candidateValues.size())
		{
			return Result<void>::Failure("Selected candidate is no longer present in the picker result set");
		}
		if (transactionHandled)
		{
			*transactionHandled = true;
		}
		return transaction.previewCandidate(candidateValues[index]);
	};
	mapped.restore = [&transaction, transactionHandled](const SerializedJson& checkpoint)
	{ return RestoreReferencePickerTransaction(transaction, transactionHandled, checkpoint); };
	return mapped;
}

Result<void> RestoreReferencePickerTransaction(const ReferencePickerTransaction& transaction, bool* transactionHandled,
                                               const SerializedJson& checkpoint)
{
	if (transactionHandled)
	{
		*transactionHandled = true;
	}
	return transaction.restore ? transaction.restore(checkpoint)
	                           : Result<void>::Failure("Reference picker transaction has no restore callback");
}

AssetPreviewKind ReferencePreviewKind(const String& kind)
{
	if (kind == "Material")
	{
		return AssetPreviewKind::Material;
	}
	if (kind == "Texture")
	{
		return AssetPreviewKind::Texture;
	}
	if (kind == "Mesh")
	{
		return AssetPreviewKind::Mesh;
	}
	if (kind == "Prefab")
	{
		return AssetPreviewKind::Prefab;
	}
	if (kind == "Model")
	{
		return AssetPreviewKind::Model;
	}
	if (kind == "Animation")
	{
		return AssetPreviewKind::Animation;
	}
	return AssetPreviewKind::None;
}

bool DrawSerializedAssetReference(nlohmann::json& value, ProjectWorkspace& workspace,
                                  const ComponentInspectorContext& context,
                                  const ReferencePickerTransaction* transaction, bool* transactionHandled)
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
	Vector<ReferencePickerOption> options;
	options.push_back({"none", "None", "None", {}, true});
	for (const auto& object : choices)
	{
		const String key = "asset:" + object.guid.ToString() + ":" + ToUrsaString(std::to_string(object.localFileId));
		ReferencePickerOption candidate{key, object.Label(), object.kind, object.sourcePath.ToString(), true};
		const AssetPreviewKind previewKind = ReferencePreviewKind(object.kind);
		if (previewKind != AssetPreviewKind::None)
		{
			AssetPreviewTarget target;
			target.kind = previewKind;
			target.identity = {object.guid, object.localFileId};
			target.path = object.sourcePath;
			target.projectSession = ResourceId::Generate();
			candidate.loadAssetPreviewContent = [&workspace, target]()
			{ return LoadAssetPreviewContent(target, workspace); };
		}
		options.push_back(std::move(candidate));
	}
	ReferencePickerTransaction pickerTransaction;
	const ReferencePickerTransaction* activeTransaction = nullptr;
	if (transaction)
	{
		Vector<SerializedJson> candidateValues{nullptr};
		candidateValues.reserve(choices.size() + 1);
		for (const auto& object : choices)
		{
			candidateValues.push_back(AssetReferenceToJson({object.guid, object.localFileId}));
		}
		pickerTransaction = MapPickerValues(*transaction, transactionHandled, std::move(candidateValues));
		activeTransaction = &pickerTransaction;
	}
	auto locateAsset = [&workspace, &context, current]() -> Result<void>
	{
		if (!context.selection)
		{
			return Result<void>::Failure("This asset field has no active editor selection context");
		}
		const auto choices = AssetObjectChoices(workspace);
		const auto found =
		    std::find_if(choices.begin(), choices.end(), [&](const ImportedArtifactRecord& object)
		                 { return object.guid == current.guid && object.localFileId == current.localFileId; });
		if (found == choices.end())
		{
			return Result<void>::Failure("The referenced asset no longer exists in the Project view");
		}
		return context.selection->SelectAsset(found->sourcePath, found->localFileId, found->kind, found->guid)
		           ? Result<void>{}
		           : Result<void>::Failure("The editor refused to navigate to the referenced asset");
	};
	SerializedJson acceptedCheckpoint;
	const auto selected = DrawReferencePickerButton(
	    "serialized-asset:" + ToUrsaString(std::to_string(ImGui::GetID("##Asset"))), preview, "Asset", options,
	    current.guid ? "asset:" + current.guid.ToString() + ":" + ToUrsaString(std::to_string(current.localFileId))
	                 : String{},
	    {}, activeTransaction, &acceptedCheckpoint,
	    current.guid ? std::function<Result<void>()>(locateAsset) : std::function<Result<void>()>{});
	bool changed = false;
	if (selected)
	{
		if (*selected == 0)
		{
			value = nullptr;
		}
		else
		{
			const auto& object = choices[*selected - 1];
			value = AssetReferenceToJson({object.guid, object.localFileId});
		}
		changed = true;
		if (transaction && transaction->applyCandidate)
		{
			auto applied = transaction->applyCandidate(value, acceptedCheckpoint);
			if (transactionHandled)
			{
				*transactionHandled = true;
			}
			if (!applied)
			{
				ImGui::TextWrapped("Cannot assign reference: %s", applied.error.c_str());
				auto restored = transaction->restore
				                    ? transaction->restore(acceptedCheckpoint)
				                    : Result<void>::Failure("Reference picker transaction has no restore callback");
				if (!restored)
				{
					ImGui::TextWrapped("Rollback failed: %s", restored.error.c_str());
				}
				changed = false;
			}
			else
			{
				changed = applied.value;
			}
		}
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
                                             const Vector<ScriptTypeDescriptor>* scriptTypes,
                                             const ReferencePickerTransaction* transaction, bool* transactionHandled)
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
	const auto choices =
	    QueryAssetObjectChoices(workspace, [](const ImportedArtifactRecord& object)
	                            { return object.kind == "ScriptableObject" || object.kind == "ScriptableAsset"; });
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
			if (!scriptTypes || !IsScriptReferenceTypeCompatible(*scriptTypes, asset->TypeId(), expectedType,
			                                                     ScriptTypeKind::ScriptableObject))
			{
				return false;
			}
		}
		return true;
	};
	const auto serializeReference = [](AssetReference reference)
	{
		return SerializedJson{{"$ursaReference",
		                       {{"Kind", "ScriptableObject"},
		                        {"Id", ""},
		                        {"Resource", {{"kind", "asset"}, {"asset", AssetReferenceToJson(reference)}}}}}};
	};
	const auto assign = [&](AssetReference reference) { value = serializeReference(reference); };
	Vector<ReferencePickerOption> options;
	Vector<ImportedArtifactRecord> compatibleChoices;
	options.push_back({"none", "None", "None", {}, true});
	for (const auto& object : choices)
	{
		if (!compatible(object))
		{
			continue;
		}
		const auto key =
		    "scriptable:" + object.guid.ToString() + ":" + ToUrsaString(std::to_string(object.localFileId));
		options.push_back({key, object.Label(), object.kind, object.sourcePath.ToString(), true});
		compatibleChoices.push_back(object);
	}
	ReferencePickerTransaction pickerTransaction;
	const ReferencePickerTransaction* activeTransaction = nullptr;
	if (transaction)
	{
		Vector<SerializedJson> candidateValues{nullptr};
		candidateValues.reserve(compatibleChoices.size() + 1);
		for (const auto& object : compatibleChoices)
		{
			candidateValues.push_back(serializeReference({object.guid, object.localFileId}));
		}
		pickerTransaction = MapPickerValues(*transaction, transactionHandled, std::move(candidateValues));
		activeTransaction = &pickerTransaction;
	}
	SerializedJson acceptedCheckpoint;
	const auto selected = DrawReferencePickerButton(
	    "scriptable-reference:" + ToUrsaString(std::to_string(ImGui::GetID("##ScriptableAsset"))), preview,
	    expectedType.empty() ? "ScriptableObject" : expectedType, options,
	    current.guid ? "scriptable:" + current.guid.ToString() + ":" + ToUrsaString(std::to_string(current.localFileId))
	                 : String{},
	    {}, activeTransaction, &acceptedCheckpoint);
	bool changed = false;
	if (selected)
	{
		if (*selected == 0)
		{
			value = nullptr;
		}
		else
		{
			const auto& object = compatibleChoices[*selected - 1];
			assign({object.guid, object.localFileId});
		}
		changed = true;
		if (transaction && transaction->applyCandidate)
		{
			auto applied = transaction->applyCandidate(value, acceptedCheckpoint);
			if (transactionHandled)
			{
				*transactionHandled = true;
			}
			if (!applied)
			{
				ImGui::TextWrapped("Cannot assign reference: %s", applied.error.c_str());
				auto restored = RestoreReferencePickerTransaction(*transaction, transactionHandled, acceptedCheckpoint);
				if (!restored)
				{
					ImGui::TextWrapped("Rollback failed: %s", restored.error.c_str());
				}
				changed = false;
			}
			else
			{
				changed = applied.value;
			}
		}
	}
	AssetReference dropped;
	if (AcceptAssetDrop(choices, dropped, compatible))
	{
		assign(dropped);
		changed = true;
	}
	return changed;
}

bool DrawReferenceCollectionDrop(SerializedJson& value, const String& elementKind, const SerializedJson& descriptor,
                                 ProjectWorkspace& workspace, const ComponentInspectorContext& context,
                                 const Vector<ScriptTypeDescriptor>* scriptTypes,
                                 const ReferencePickerTransaction* transaction, bool* transactionHandled)
{
	ImGui::Button("Drop references here to append");
	bool changed = false;
	if (!ImGui::BeginDragDropTarget())
	{
		return false;
	}
	if (const auto* payload = AcceptEditorDragDropPayload())
	{
		auto decoded = DecodeEditorDragPayload(payload->DataType, payload->Data, payload->DataSize);
		Vector<SerializedJson> additions;
		String failure;
		if (!decoded)
		{
			failure = decoded.error;
		}
		else if (decoded.value.sources.empty())
		{
			failure = "The drag contains no references.";
		}
		else
		{
			for (const EditorDragSource& source : decoded.value.sources)
			{
				if (elementKind == "GameObjectReference")
				{
					const auto& identity = source.identity;
					if (identity.kind != EditorSelectionKind::GameObject || source.componentId || !context.documentId ||
					    identity.documentId != context.documentId)
					{
						failure = "Every dropped item must be a GameObject from the current document.";
						break;
					}
					GameObject::Ptr found;
					Vector<GameObject::Ptr> pending = context.roots;
					while (!pending.empty() && !found)
					{
						auto object = std::move(pending.back());
						pending.pop_back();
						if (object->Id() == identity.objectId)
						{
							found = object;
							break;
						}
						pending.insert(pending.end(), object->Children().begin(), object->Children().end());
					}
					if (!found)
					{
						failure = "A dropped GameObject is no longer available.";
						break;
					}
					ResourceId id = context.scene ? context.scene->PersistentId(found).value : found->Id();
					SerializedJson reference = SerializedJson::object();
					reference["$ursaReference"] = {{"Kind", "GameObject"}, {"Id", ToStdString(id.ToString())}};
					additions.push_back(std::move(reference));
				}
				else if (elementKind == "ComponentReference")
				{
					if (source.identity.kind != EditorSelectionKind::GameObject || !context.documentId ||
					    source.identity.documentId != context.documentId)
					{
						failure = "Every dropped item must be a compatible component from the current document.";
						break;
					}
					Component* component = nullptr;
					GameObject::Ptr owner;
					if (source.componentId)
					{
						Vector<GameObject::Ptr> pending = context.roots;
						while (!pending.empty() && !owner)
						{
							auto object = std::move(pending.back());
							pending.pop_back();
							if (object->Id() == source.identity.objectId)
							{
								owner = object;
								break;
							}
							pending.insert(pending.end(), object->Children().begin(), object->Children().end());
						}
						if (owner)
						{
							const auto compatible = CompatibleComponentsForReference(
							    *owner, ToUrsaString(descriptor.value("ElementTypeName", std::string{})), scriptTypes);
							for (Component* candidate : compatible)
							{
								ResourceId id = candidate->PersistentId();
								if (context.scene)
								{
									auto persistent = context.scene->ComponentPersistentId(candidate->Id());
									if (persistent)
									{
										id = persistent.value;
									}
								}
								if (id == source.componentId)
								{
									component = candidate;
									break;
								}
							}
						}
					}
					else
					{
						Vector<GameObject::Ptr> pending = context.roots;
						while (!pending.empty() && !owner)
						{
							auto object = std::move(pending.back());
							pending.pop_back();
							if (object->Id() == source.identity.objectId)
							{
								owner = object;
								break;
							}
							pending.insert(pending.end(), object->Children().begin(), object->Children().end());
						}
						if (owner)
						{
							const auto compatible = CompatibleComponentsForReference(
							    *owner, ToUrsaString(descriptor.value("ElementTypeName", std::string{})), scriptTypes);
							if (compatible.size() == 1)
							{
								component = compatible.front();
							}
							else
							{
								failure = compatible.empty() ? "A dropped GameObject has no compatible component."
								                             : "Drag a specific component for ambiguous references.";
							}
						}
					}
					if (failure.empty() && component && owner && component->GetGameObject() == owner)
					{
						const auto compatible = CompatibleComponentsForReference(
						    *owner, ToUrsaString(descriptor.value("ElementTypeName", std::string{})), scriptTypes);
						if (std::find(compatible.begin(), compatible.end(), component) == compatible.end())
						{
							failure = "A dragged component does not match the collection element type.";
						}
						else
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
							SerializedJson reference = SerializedJson::object();
							reference["$ursaReference"] = {{"Kind", "Component"}, {"Id", ToStdString(id.ToString())}};
							additions.push_back(std::move(reference));
						}
					}
					else if (failure.empty())
					{
						failure = "A dragged component is no longer available.";
					}
				}
				else if (elementKind == "AssetReference" || elementKind == "ScriptableObjectReference" ||
				         elementKind == "MeshReference" || elementKind == "MaterialReference" ||
				         elementKind == "TextureReference" || elementKind == "ShaderReference" ||
				         elementKind == "ComputeShaderReference")
				{
					if (source.identity.kind != EditorSelectionKind::Asset || !source.identity.assetGuid)
					{
						failure = "Every dropped item must be an asset reference of the collection element type.";
						break;
					}
					const auto choices = AssetObjectChoices(workspace);
					const auto object =
					    std::find_if(choices.begin(), choices.end(),
					                 [&](const auto& candidate)
					                 {
						                 return candidate.guid == source.identity.assetGuid &&
						                        (candidate.localFileId == source.identity.assetLocalFileId ||
						                         (!source.identity.assetLocalFileId && candidate.isMainObject));
					                 });
					if (object == choices.end() || (elementKind == "MeshReference" && object->kind != "Mesh") ||
					    (elementKind == "MaterialReference" && object->kind != "Material") ||
					    (elementKind == "TextureReference" && object->kind != "Texture") ||
					    (elementKind == "ScriptableObjectReference" && object->kind != "ScriptableObject" &&
					     object->kind != "ScriptableAsset") ||
					    (elementKind == "ShaderReference" && object->kind != "Shader") ||
					    (elementKind == "ComputeShaderReference" && object->kind != "ComputeShader"))
					{
						failure = "A dropped asset is missing or incompatible with the collection element type.";
						break;
					}
					const AssetReference reference{object->guid, object->localFileId};
					if (elementKind == "AssetReference")
					{
						additions.push_back(AssetReferenceToJson(reference));
					}
					else if (elementKind == "TextureReference" || elementKind == "ScriptableObjectReference" ||
					         elementKind == "ShaderReference" || elementKind == "ComputeShaderReference")
					{
						const String refKind = elementKind.substr(0, elementKind.size() - String("Reference").size());
						SerializedJson referenceValue = SerializedJson::object();
						referenceValue["$ursaReference"] = {
						    {"Kind", ToStdString(refKind)}, {"Id", ""}, {"Resource", AssetReferenceToJson(reference)}};
						additions.push_back(std::move(referenceValue));
					}
					else
					{
						const String refKind = elementKind == "MeshReference"       ? "Mesh"
						                       : elementKind == "MaterialReference" ? "Material"
						                                                            : "ScriptableObject";
						additions.push_back(
						    {{"$ursaReference",
						      {{"Kind", ToStdString(refKind)},
						       {"Id", ""},
						       {"Resource", {{"kind", "asset"}, {"asset", AssetReferenceToJson(reference)}}}}}});
					}
				}
				else
				{
					failure = "This collection element type does not accept drag and drop.";
					break;
				}
			}
		}
		if (!failure.empty())
		{
			ImGui::SetTooltip("%s", failure.c_str());
		}
		else if (transaction && transaction->appendReferences)
		{
			auto appended = transaction->appendReferences(additions);
			if (!appended)
			{
				ImGui::TextWrapped("Cannot append references: %s", appended.error.c_str());
			}
			else
			{
				changed = appended.value;
				if (transactionHandled)
				{
					*transactionHandled = true;
				}
			}
		}
		else
		{
			if (!value.is_array())
			{
				value = SerializedJson::array();
			}
			for (auto& addition : additions)
			{
				value.push_back(std::move(addition));
			}
			changed = true;
		}
	}
	ImGui::EndDragDropTarget();
	return changed;
}

bool DrawSerializedScriptValue(nlohmann::json& value, const String& kind, const String& elementKind,
                               const nlohmann::json& descriptor, ProjectWorkspace& workspace,
                               const ComponentInspectorContext& context,
                               const Vector<ScriptTypeDescriptor>* scriptTypes,
                               const ReferencePickerTransaction* transaction, bool* transactionHandled)
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
		return DrawSerializedAssetReference(value, workspace, context, transaction, transactionHandled);
	}
	else if (kind == "ScriptableObjectReference")
	{
		const bool collection =
		    descriptor.value("Kind", std::string{}) == "Array" || descriptor.value("Kind", std::string{}) == "List";
		const String expectedType =
		    ToUrsaString(descriptor.value(collection ? "ElementTypeName" : "TypeName", std::string{}));
		return DrawSerializedScriptableObjectReference(value, expectedType, workspace, scriptTypes, transaction,
		                                               transactionHandled);
	}
	else if (kind == "MeshReference" || kind == "MaterialReference")
	{
		const auto expected = kind == "MeshReference" ? ResourceKind::Mesh : ResourceKind::Material;
		String preview = "None / Local";
		String currentKey;
		if (value.is_object() && value.contains("$ursaReference") && value["$ursaReference"].contains("Resource"))
		{
			const auto& binding = value["$ursaReference"]["Resource"];
			if (binding.is_object() && binding.value("kind", std::string{}) == "asset")
			{
				preview = "Missing asset";
				const auto reference = AssetReferenceFromJson(binding["asset"], "Inspector resource");
				if (reference)
				{
					currentKey = "resource:" + reference.value.guid.ToString() + ":" +
					             ToUrsaString(std::to_string(reference.value.localFileId));
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
		const auto serializeReference = [&](AssetReference reference)
		{
			return SerializedJson{{"$ursaReference",
			                       {{"Kind", expected == ResourceKind::Mesh ? "Mesh" : "Material"},
			                        {"Id", ""},
			                        {"Resource", {{"kind", "asset"}, {"asset", AssetReferenceToJson(reference)}}}}}};
		};
		const auto assign = [&](AssetReference reference) { value = serializeReference(reference); };
		Vector<ReferencePickerOption> options;
		Vector<ImportedArtifactRecord> compatibleChoices;
		options.push_back({"none", "None", "None", {}, true});
		for (const auto& object : choices)
		{
			if (object.kind == (expected == ResourceKind::Mesh ? "Mesh" : "Material"))
			{
				const auto key =
				    "resource:" + object.guid.ToString() + ":" + ToUrsaString(std::to_string(object.localFileId));
				options.push_back({key, object.Label(), object.kind, object.sourcePath.ToString(), true});
				compatibleChoices.push_back(object);
			}
		}
		ReferencePickerTransaction pickerTransaction;
		const ReferencePickerTransaction* activeTransaction = nullptr;
		if (transaction)
		{
			Vector<SerializedJson> candidateValues{nullptr};
			candidateValues.reserve(compatibleChoices.size() + 1);
			for (const auto& object : compatibleChoices)
			{
				candidateValues.push_back(serializeReference({object.guid, object.localFileId}));
			}
			pickerTransaction = MapPickerValues(*transaction, transactionHandled, std::move(candidateValues));
			activeTransaction = &pickerTransaction;
		}
		SerializedJson acceptedCheckpoint;
		const auto selected =
		    DrawReferencePickerButton("serialized-resource:" + ToUrsaString(std::to_string(ImGui::GetID("##Resource"))),
		                              preview, expected == ResourceKind::Mesh ? "Mesh" : "Material", options,
		                              currentKey, {}, activeTransaction, &acceptedCheckpoint);
		bool changed = false;
		if (selected)
		{
			if (*selected == 0)
			{
				value = nullptr;
				changed = true;
			}
			else
			{
				const auto& object = compatibleChoices[*selected - 1];
				if (compatible(object))
				{
					assign({object.guid, object.localFileId});
					changed = true;
				}
			}
			if (changed && transaction && transaction->applyCandidate)
			{
				auto applied = transaction->applyCandidate(value, acceptedCheckpoint);
				if (transactionHandled)
				{
					*transactionHandled = true;
				}
				if (!applied)
				{
					ImGui::TextWrapped("Cannot assign reference: %s", applied.error.c_str());
					auto restored =
					    RestoreReferencePickerTransaction(*transaction, transactionHandled, acceptedCheckpoint);
					if (!restored)
					{
						ImGui::TextWrapped("Rollback failed: %s", restored.error.c_str());
					}
					changed = false;
				}
				else
				{
					changed = applied.value;
				}
			}
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
		return DrawSerializedReference(
		    value, kind, ToUrsaString(descriptor.value("TypeName", descriptor.value("ElementTypeName", std::string{}))),
		    context, scriptTypes, transaction, transactionHandled);
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
		if (elementKind == "AssetReference" || elementKind == "ScriptableObjectReference" ||
		    elementKind == "MeshReference" || elementKind == "MaterialReference" || elementKind == "TextureReference" ||
		    elementKind == "ShaderReference" || elementKind == "ComputeShaderReference" ||
		    elementKind == "GameObjectReference" || elementKind == "ComponentReference")
		{
			changed |= DrawReferenceCollectionDrop(value, elementKind, descriptor, workspace, context, scriptTypes,
			                                       transaction, transactionHandled);
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

bool DrawSerializedScriptFields(Scripting::ScriptBehaviourComponent& component, ProjectWorkspace& workspace,
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

Result<void> DeleteSceneObjects(Scene& scene, const Vector<ResourceId>& selectedIds, ResourceId,
                                ComponentRegistry& registry, SceneDraft& draft, EditorSelectionContext* selection)
{
	Set<String> selectedIdsText;
	for (const auto& id : selectedIds)
	{
		if (id)
		{
			selectedIdsText.insert(id.ToString());
		}
	}
	Vector<GameObject::Ptr> roots;
	Set<String> unique;
	for (const auto& id : selectedIds)
	{
		if (!id || !unique.insert(id.ToString()).second)
		{
			continue;
		}
		auto object = scene.GameObjectById(id);
		if (!object)
		{
			return Result<void>::Failure(object.error);
		}
		auto parent = scene.ParentOf(object.value);
		if (!parent)
		{
			return Result<void>::Failure(parent.error);
		}
		bool covered = false;
		for (auto ancestor = parent.value; ancestor;)
		{
			if (selectedIdsText.count(ancestor->Id().ToString()))
			{
				covered = true;
				break;
			}
			auto next = scene.ParentOf(ancestor);
			if (!next)
			{
				return Result<void>::Failure(next.error);
			}
			ancestor = next.value;
		}
		if (!covered)
		{
			roots.push_back(object.value);
		}
	}
	if (roots.empty())
	{
		return Result<void>::Failure("Select at least one editable GameObject to delete");
	}
	Set<String> deletedObjects;
	Set<String> deletedComponents;
	std::function<Result<void>(const GameObject::Ptr&)> collect = [&](const GameObject::Ptr& object) -> Result<void>
	{
		deletedObjects.insert(object->Id().ToString());
		auto components = scene.ComponentsOf(object);
		if (!components)
		{
			return Result<void>::Failure(components.error);
		}
		for (ComponentId component : components.value)
		{
			auto id = scene.ComponentPersistentId(component);
			if (!id)
			{
				return Result<void>::Failure(id.error);
			}
			deletedComponents.insert(id.value.ToString());
		}
		for (const auto& child : object->Children())
		{
			auto result = collect(child);
			if (!result)
			{
				return result;
			}
		}
		return {};
	};
	for (const auto& root : roots)
	{
		auto result = collect(root);
		if (!result)
		{
			return result;
		}
	}
	const MaterialIdResolver resolver = [&draft](const std::shared_ptr<MaterialInstance>& material)
	{ return draft.MaterialId(material); };
	auto document = EncodeSceneDocument("delete.ursascene", ResourceId::Generate(), scene, registry, resolver);
	if (!document)
	{
		return Result<void>::Failure(document.error);
	}
	std::function<Result<void>(const nlohmann::json&, bool, const String&)> inspectReferences =
	    [&](const nlohmann::json& value, bool ownerDeleted, const String& location) -> Result<void>
	{
		if (value.is_array())
		{
			for (Ursa::Size index = 0; index < value.size(); ++index)
			{
				auto result =
				    inspectReferences(value[index], ownerDeleted, location + "/" + ToUrsaString(std::to_string(index)));
				if (!result)
				{
					return result;
				}
			}
		}
		else if (value.is_object())
		{
			if (value.contains("$ursaReference") && value["$ursaReference"].is_object())
			{
				const auto& reference = value["$ursaReference"];
				if (reference.contains("Kind") && reference["Kind"].is_string() && reference.contains("Id") &&
				    reference["Id"].is_string())
				{
					const String kind = ToUrsaString(reference["Kind"].get<std::string>());
					const String target = ToUrsaString(reference["Id"].get<std::string>());
					const bool dangling = kind == "GameObject"  ? deletedObjects.count(target) != 0
					                      : kind == "Component" ? deletedComponents.count(target) != 0
					                                            : false;
					if (dangling && !ownerDeleted)
					{
						return Result<void>::Failure("Cannot delete referenced " + kind + " " + target +
						                             ": serialized reference at " + location);
					}
				}
			}
			for (const auto& entry : value.items())
			{
				auto result =
				    inspectReferences(entry.value(), ownerDeleted, location + "/" + ToUrsaString(entry.key()));
				if (!result)
				{
					return result;
				}
			}
		}
		return {};
	};
	for (const auto& object : document.value["gameObjects"])
	{
		const String ownerId = ToUrsaString(object["id"].get<std::string>());
		for (const auto& component : object["components"])
		{
			const String type = ToUrsaString(component["type"].get<std::string>());
			auto checked =
			    inspectReferences(component["data"], deletedObjects.count(ownerId) != 0, ownerId + "/" + type);
			if (!checked)
			{
				return checked;
			}
		}
	}
	LinkedSceneResource* linked = draft.LinkedScene();
	const auto linkedBefore = linked ? linked->document : nlohmann::json{};
	if (linked)
	{
		Vector<ResourceId> deletedObjectIds;
		for (const auto& id : deletedObjects)
		{
			auto parsed = ResourceId::Parse(id);
			if (!parsed)
			{
				return Result<void>::Failure(parsed.error);
			}
			deletedObjectIds.push_back(parsed.value);
		}
		auto updated = RecordPrefabDeletionOverrides(*linked, deletedObjectIds);
		if (!updated)
		{
			return updated;
		}
	}
	for (const auto& root : roots)
	{
		auto removed = scene.DestroyGameObject(root);
		if (!removed)
		{
			if (linked)
			{
				linked->document = linkedBefore;
			}
			return removed;
		}
	}
	if (selection)
	{
		Vector<EditorSelectionEntry> survivors;
		for (const auto& entry : selection->entries)
		{
			if (entry.kind == EditorSelectionKind::GameObject && entry.documentId == selection->activeDocumentId &&
			    deletedObjects.count(entry.objectId.ToString()) == 0)
			{
				survivors.push_back(entry);
			}
		}
		selection->ReconcileEntries(std::move(survivors));
	}
	return {};
}

Result<void> DuplicateSceneObjects(Scene& scene, const Vector<ResourceId>& selectedIds, ResourceId primaryId,
                                   ComponentRegistry& registry, EditorDocuments& documents, SceneDraft& draft,
                                   EditorSelectionContext* selection)
{
	struct Target
	{
		GameObject::Ptr object;
		GameObject::Ptr parent;
		Ursa::Size siblingIndex;
	};

	Set<String> selected;
	for (const auto& id : selectedIds)
	{
		if (id)
		{
			selected.insert(id.ToString());
		}
	}
	Vector<Target> targets;
	Set<String> unique;
	for (const auto& id : selectedIds)
	{
		if (!id || !unique.insert(id.ToString()).second)
		{
			continue;
		}
		auto object = scene.GameObjectById(id);
		if (!object)
		{
			return Result<void>::Failure(object.error);
		}
		auto parent = scene.ParentOf(object.value);
		if (!parent)
		{
			return Result<void>::Failure(parent.error);
		}
		bool covered = false;
		for (auto ancestor = parent.value; ancestor;)
		{
			if (selected.count(ancestor->Id().ToString()))
			{
				covered = true;
				break;
			}
			auto next = scene.ParentOf(ancestor);
			if (!next)
			{
				return Result<void>::Failure(next.error);
			}
			ancestor = next.value;
		}
		if (covered)
		{
			continue;
		}
		auto siblings = parent.value ? scene.ChildrenOf(parent.value)
		                             : Result<Vector<GameObject::Ptr>>::Success(scene.RootGameObjects());
		if (!siblings)
		{
			return Result<void>::Failure(siblings.error);
		}
		auto position = std::find(siblings.value.begin(), siblings.value.end(), object.value);
		if (position == siblings.value.end())
		{
			return Result<void>::Failure("A selected GameObject is no longer in its sibling list");
		}
		targets.push_back({object.value, parent.value, static_cast<Ursa::Size>(position - siblings.value.begin())});
	}
	if (targets.empty())
	{
		return Result<void>::Failure("Select at least one editable GameObject to duplicate");
	}
	Vector<GameObject::Ptr> roots;
	Ursa::Size primaryIndex = 0;
	for (Ursa::Size index = 0; index < targets.size(); ++index)
	{
		roots.push_back(targets[index].object);
		if (targets[index].object->Id() == primaryId)
		{
			primaryIndex = index;
		}
	}
	ComponentLoadContext context(documents.Session());
	if (draft.ContextSetup())
	{
		auto prepared = draft.ContextSetup()(context);
		if (!prepared)
		{
			return prepared;
		}
	}
	const MaterialIdResolver materialId = [&draft](const std::shared_ptr<MaterialInstance>& material)
	{ return draft.MaterialId(material); };
	auto prefab = CapturePrefabAsset(ResourceId::Generate(), scene, roots, registry, context, materialId);
	if (!prefab)
	{
		return Result<void>::Failure(prefab.error);
	}
	context.ClearMappings();
	Set<String> copiedIds;
	std::function<void(const GameObject::Ptr&)> markCopied = [&](const GameObject::Ptr& object)
	{
		copiedIds.insert(object->Id().ToString());
		for (const auto& child : object->Children())
		{
			markCopied(child);
		}
	};
	for (const auto& root : roots)
	{
		markCopied(root);
	}
	std::function<Result<void>(const GameObject::Ptr&)> mapExternal = [&](const GameObject::Ptr& object) -> Result<void>
	{
		if (copiedIds.count(object->Id().ToString()) == 0)
		{
			auto mappedObject = context.MapGameObject(object->Id(), object);
			if (!mappedObject)
			{
				return mappedObject;
			}
			auto components = scene.ComponentsOf(object);
			if (!components)
			{
				return Result<void>::Failure(components.error);
			}
			for (ComponentId componentId : components.value)
			{
				auto identity = scene.ComponentPersistentId(componentId);
				if (!identity)
				{
					return Result<void>::Failure(identity.error);
				}
				auto mappedComponent = context.MapComponent(identity.value, componentId);
				if (!mappedComponent)
				{
					return mappedComponent;
				}
				auto mappedComponentObject =
				    context.MapComponentObject(identity.value, scene.GetComponent(componentId));
				if (!mappedComponentObject)
				{
					return mappedComponentObject;
				}
			}
		}
		for (const auto& child : object->Children())
		{
			auto result = mapExternal(child);
			if (!result)
			{
				return result;
			}
		}
		return {};
	};
	for (const auto& root : scene.RootGameObjects())
	{
		auto mapped = mapExternal(root);
		if (!mapped)
		{
			return mapped;
		}
	}
	auto wrapper = prefab.value.Instantiate(scene, registry, context, materialId, true);
	if (!wrapper)
	{
		return Result<void>::Failure(wrapper.error);
	}
	auto copies = scene.ChildrenOf(wrapper.value);
	if (!copies || copies.value.size() != targets.size())
	{
		scene.DestroyGameObject(wrapper.value);
		return Result<void>::Failure(copies ? "The duplicate graph is incomplete" : copies.error);
	}
	auto rollback = [&]
	{
		for (const auto& copy : copies.value)
		{
			if (scene.Contains(copy))
			{
				scene.DestroyGameObject(copy);
			}
		}
		if (scene.Contains(wrapper.value))
		{
			scene.DestroyGameObject(wrapper.value);
		}
	};
	Map<String, Ursa::Size> insertedByParent;
	for (Ursa::Size index = 0; index < copies.value.size(); ++index)
	{
		auto parented = scene.SetParent(copies.value[index], targets[index].parent);
		if (!parented)
		{
			rollback();
			return parented;
		}
		const String parentKey = targets[index].parent ? targets[index].parent->Id().ToString() : String{};
		const Ursa::Size inserted = insertedByParent[parentKey]++;
		auto reordered = scene.ReorderGameObject(copies.value[index], targets[index].siblingIndex + 1 + inserted);
		if (!reordered)
		{
			rollback();
			return reordered;
		}
	}
	auto removedWrapper = scene.DestroyGameObject(wrapper.value);
	if (!removedWrapper)
	{
		rollback();
		return removedWrapper;
	}
	if (selection)
	{
		Vector<EditorSelectionEntry> entries;
		for (const auto& copy : copies.value)
		{
			EditorSelectionEntry entry;
			entry.kind = EditorSelectionKind::GameObject;
			entry.documentId = selection->activeDocumentId;
			entry.objectId = copy->Id();
			entries.push_back(entry);
		}
		if (!selection->SelectEntries(std::move(entries), primaryIndex))
		{
			rollback();
			return Result<void>::Failure("The new objects could not become the active selection");
		}
	}
	return {};
}
} // namespace

Result<void> DeleteGameObjectSelection(Scene& scene, const Vector<ResourceId>& selectedIds, ComponentRegistry& registry,
                                       SceneDraft& draft, EditorSelectionContext* selection)
{
	return DeleteSceneObjects(scene, selectedIds, {}, registry, draft, selection);
}

Result<void> RecordPrefabDeletionOverrides(LinkedSceneResource& linked, const Vector<ResourceId>& deletedIds)
{
	Set<String> deleted;
	for (const auto& id : deletedIds)
	{
		if (id)
		{
			deleted.insert(id.ToString());
		}
	}
	Map<String, Vector<String>> prefabDeletes;
	for (const auto& scope : linked.scopedIds)
	{
		for (const auto& mapping : scope.second)
		{
			if (deleted.count(mapping.second) != 0)
			{
				prefabDeletes[scope.first].push_back(mapping.first);
			}
		}
	}
	const auto previous = linked.document;
	for (const auto& instance : prefabDeletes)
	{
		auto instanceId = ResourceId::Parse(instance.first);
		if (!instanceId)
		{
			linked.document = previous;
			return Result<void>::Failure(instanceId.error);
		}
		nlohmann::json removed = nlohmann::json::array();
		if (linked.document.contains("instances") && linked.document["instances"].is_array())
		{
			for (const auto& row : linked.document["instances"])
			{
				if (row.value("id", std::string{}) == ToStdString(instance.first) && row.contains("overrides") &&
				    row["overrides"].is_object() && row["overrides"].contains("deletedGameObjects") &&
				    row["overrides"]["deletedGameObjects"].is_array())
				{
					removed = row["overrides"]["deletedGameObjects"];
					break;
				}
			}
		}
		for (const auto& sourceId : instance.second)
		{
			if (std::find(removed.begin(), removed.end(), ToStdString(sourceId)) == removed.end())
			{
				removed.push_back(ToStdString(sourceId));
			}
		}
		auto changed = SetPrefabStructureOverride(linked, instanceId.value, "deletedGameObjects", removed);
		if (!changed)
		{
			linked.document = previous;
			return changed;
		}
	}
	return {};
}

Result<void> AddComponentToSelection(const Vector<GameObject::Ptr>& selectedObjects, ComponentEditors& componentEditors,
                                     const String& type, const ComponentInspectorContext& baseContext)
{
	if (selectedObjects.empty())
	{
		return Result<void>::Failure("Select at least one GameObject before adding a component");
	}
	Set<GameObject*> unique;
	for (const auto& object : selectedObjects)
	{
		if (!object || !unique.insert(object.get()).second)
		{
			return Result<void>::Failure("Component selection contains an empty or duplicate GameObject");
		}
	}
	const auto* descriptor = componentEditors.FindType(type);
	if (!descriptor)
	{
		return Result<void>::Failure("Unregistered component type " + type);
	}
	for (const auto& object : selectedObjects)
	{
		ComponentInspectorContext validation = baseContext;
		validation.object = object;
		auto available = componentEditors.CanCreate(type, validation);
		if (!available)
		{
			const auto name = object->Name();
			return Result<void>::Failure((name ? name.value : String("GameObject")) + ": " + available.error);
		}
	}

	Vector<std::unique_ptr<Component>> candidates;
	candidates.reserve(selectedObjects.size());
	for (const auto& target : selectedObjects)
	{
		auto staging = GameObject::Create("Component Candidate");
		ComponentInspectorContext context = baseContext;
		context.object = staging;
		auto created = componentEditors.Create(type, context);
		if (!created)
		{
			const auto name = target->Name();
			return Result<void>::Failure((name ? name.value : String("GameObject")) + ": " + created.error);
		}
		auto released = staging->ReleaseDetachedComponent(created.value);
		if (!released)
		{
			return Result<void>::Failure(released.error);
		}
		candidates.push_back(std::move(released.value));
	}

	Vector<Component*> attached;
	attached.reserve(selectedObjects.size());
	for (Size index = 0; index < selectedObjects.size(); ++index)
	{
		auto adopted = selectedObjects[index]->AdoptDetachedComponent(std::move(candidates[index]));
		if (!adopted)
		{
			for (Component* component : attached)
			{
				if (component && component->GetGameObject())
				{
					component->GetGameObject()->RemoveComponent(component);
				}
			}
			return Result<void>::Failure(adopted.error);
		}
		attached.push_back(adopted.value);
	}
	return {};
}

Result<void> RemoveCommonComponentFromSelection(const Vector<GameObject::Ptr>& selectedObjects,
                                                const ComponentTargetGroup& group)
{
	if (selectedObjects.empty() || group.targets.size() != selectedObjects.size())
	{
		return Result<void>::Failure("Common component removal requires one mapped component per selected GameObject");
	}
	Vector<GameObject::Ptr> owners;
	owners.reserve(selectedObjects.size());
	for (Size index = 0; index < selectedObjects.size(); ++index)
	{
		Component* component = group.targets[index];
		if (!selectedObjects[index] || !component || component->GetGameObject().get() != selectedObjects[index].get())
		{
			return Result<void>::Failure("Common component mapping changed before removal");
		}
		owners.push_back(selectedObjects[index]);
	}
	for (Size index = 0; index < group.targets.size(); ++index)
	{
		auto removed = owners[index]->RemoveComponent(group.targets[index]);
		if (!removed)
		{
			return removed;
		}
	}
	return {};
}

Result<std::unique_ptr<SerializedObject>> CreateGameObjectSerializedProperties(GameObject::Ptr object)
{
	return CreateGameObjectSerializedObject(std::move(object));
}

Result<bool> DrawSceneInspector(EditorDocument& document, EditorHierarchy& hierarchy, EditorDocuments& documents,
                                ProjectWorkspace& workspace, ComponentRegistry& registry,
                                ComponentEditors& componentEditors, String& diagnostic,
                                const Vector<ScriptTypeDescriptor>* scriptTypes, bool* hierarchyOpen,
                                bool* inspectorOpen, bool drawInspector, EditorSelectionContext* selection,
                                EditorHierarchy::AssetDropAction assetDrop)
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
		auto hierarchyChanged = hierarchy.Draw(*asset, hierarchyOpen, selection, document.Id(), document.Revision());
		if (!hierarchyChanged)
		{
			return hierarchyChanged;
		}
		bool changed = hierarchyChanged.value;
		auto selected = hierarchy.SelectedGameObject(*asset);
		if (drawInspector)
		{
			ImGui::Begin("Inspector", inspectorOpen);
			Icons::DrawPanelContextMenu("PrefabInspectorPanelActions", inspectorOpen);
			if (!selected)
			{
				ImGui::TextDisabled("Select a GameObject in the Prefab hierarchy.");
			}
			else
			{
				GameObject::Ptr object = selected.value;
				Vector<GameObject::Ptr> selectedObjects{object};
				if (selection && selection->entries.size() > 1)
				{
					Vector<ResourceId> selectedIds;
					for (const auto& entry : selection->entries)
					{
						if (entry.kind == EditorSelectionKind::GameObject && entry.documentId == document.Id() &&
						    !entry.playSession && !entry.IsMissing())
						{
							selectedIds.push_back(entry.objectId);
						}
					}
					selectedObjects.clear();
					CollectPrefabGameObjects(asset->Root(), selectedIds, selectedObjects);
					if (std::find(selectedObjects.begin(), selectedObjects.end(), object) == selectedObjects.end())
					{
						selectedObjects.push_back(object);
					}
				}
				Vector<std::unique_ptr<SerializedObject>> serializedTargets;
				auto group = CreateGameObjectGroup(selectedObjects, object, serializedTargets);
				if (!group)
				{
					return Result<bool>::Failure(group.error);
				}
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
				inspectorContext.selection = selection;
				inspectorContext.documentId = document.Id();
				inspectorContext.loadContext = &loadContext;
				Icons::Section(Icons::ForObject(object, object == asset->Root()), "GameObject");
				auto objectName = object->Name();
				auto groupProperties =
				    DrawGameObjectGroupProperties(group.value, objectName ? objectName.value : String{}, diagnostic);
				if (!groupProperties)
				{
					diagnostic = groupProperties.error;
				}
				else
				{
					changed |= groupProperties.value;
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
				if (selectedObjects.size() > 1)
				{
					auto summary = DrawCommonComponentSummary(selectedObjects, object, registry, componentEditors,
					                                          inspectorContext, loadContext, workspace, scriptTypes,
					                                          nullptr, draft.get());
					diagnostic = summary ? String{} : summary.error;
					changed |= summary && summary.value;
					changed |= DrawAddComponentBrowser(componentEditors, inspectorContext, diagnostic, scriptTypes,
					                                   selectedObjects);
				}
				else
				{
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
						if (inspectorContext.selection)
						{
							EditorSelectionEntry owner;
							owner.kind = EditorSelectionKind::GameObject;
							owner.documentId = inspectorContext.documentId;
							owner.objectId = object->Id();
							if (const auto ownerEntry = std::find_if(inspectorContext.selection->entries.begin(),
							                                         inspectorContext.selection->entries.end(),
							                                         [&](const auto& entry)
							                                         {
								                                         return entry.kind ==
								                                                    EditorSelectionKind::GameObject &&
								                                                entry.documentId == owner.documentId &&
								                                                entry.objectId == owner.objectId;
							                                         });
							    ownerEntry != inspectorContext.selection->entries.end())
							{
								owner = *ownerEntry;
							}
							ResourceId componentId = component->PersistentId();
							if (inspectorContext.scene)
							{
								auto persistent = inspectorContext.scene->ComponentPersistentId(component->Id());
								if (persistent)
								{
									componentId = persistent.value;
								}
							}
							if (ImGui::BeginDragDropSource())
							{
								auto drag =
								    BeginEditorComponentDragSession(owner, componentId, component->SerializationType(),
								                                    inspectorContext.selection->revision);
								if (drag)
								{
									ImGui::SetDragDropPayload(editorDragSessionPayloadType, &drag.value,
									                          sizeof(drag.value));
								}
								ImGui::TextUnformatted(component->SerializationType().c_str());
								ImGui::EndDragDropSource();
							}
						}
						bool removeFromHeader = false;
						if (ImGui::BeginPopupContextItem("ComponentHeaderActions"))
						{
							removeFromHeader = ImGui::MenuItem("Remove Component");
							ImGui::EndPopup();
						}
						bool componentEnabled = component->IsEnabled();
						if (component->SupportsEnabled() && ImGui::Checkbox("Enabled", &componentEnabled))
						{
							component->SetEnabled(componentEnabled);
							changed = true;
						}
						Icons::SameLine();
						const ComponentEditor* customEditor =
						    componentEditors.Find(scriptHeader ? scriptHeader->TypeName()
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
						if (removeFromHeader || Icons::ComponentMenu())
						{
							auto removed = object->RemoveComponent(component);
							diagnostic = removed ? String{} : removed.error;
							changed |= static_cast<bool>(removed);
						}
						ImGui::PopID();
					}
					changed |= DrawAddComponentBrowser(componentEditors, inspectorContext, diagnostic, scriptTypes,
					                                   selectedObjects);
				}
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
	EditorHierarchy::DuplicateObjectsAction duplicateObjects =
	    [&registry, &documents, draft, selection](Scene& target, const Vector<ResourceId>& ids, ResourceId primary)
	{ return DuplicateSceneObjects(target, ids, primary, registry, documents, *draft, selection); };
	EditorHierarchy::DeleteObjectsAction deleteObjects =
	    [&registry, draft, selection](Scene& target, const Vector<ResourceId>& ids, ResourceId primary)
	{ return DeleteSceneObjects(target, ids, primary, registry, *draft, selection); };
	auto hierarchyChanged =
	    hierarchy.Draw(scene, hierarchyOpen, draft->LinkedScene(), selection, document.Id(), false, document.Revision(),
	                   std::move(duplicateObjects), std::move(deleteObjects), std::move(assetDrop));
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
	Vector<GameObject::Ptr> selectedObjects;
	Vector<std::unique_ptr<SerializedObject>> serializedTargets;
	std::optional<SerializedObjectGroup> objectGroup;
	if (selected)
	{
		auto resolved = SelectedSceneObjects(scene, selected.value, selection, document.Id());
		if (!resolved)
		{
			diagnostic = resolved.error;
		}
		else
		{
			selectedObjects = std::move(resolved.value);
			auto group = CreateGameObjectGroup(selectedObjects, selected.value, serializedTargets);
			if (!group)
			{
				diagnostic = group.error;
			}
			else
			{
				objectGroup.emplace(std::move(group.value));
			}
		}
	}
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
		inspectorContext.selection = selection;
		inspectorContext.documentId = document.Id();
		inspectorContext.loadContext = &loadContext;
	}
	if (drawInspector)
	{
		ImGui::Begin("Inspector", inspectorOpen);
		Icons::DrawPanelContextMenu("SceneInspectorPanelActions", inspectorOpen);
		if (!selected)
		{
			ImGui::TextDisabled("Select a node in Hierarchy to edit its properties.");
		}
		else
		{
			GameObject::Ptr node = selected.value;
			Icons::Section(Icons::ForObject(node, hierarchy.IsPrefabObject(node->Id())), "GameObject");
			if (!objectGroup)
			{
				ImGui::TextWrapped("%s", diagnostic.empty() ? "The selected objects could not be resolved."
				                                            : diagnostic.c_str());
			}
			else
			{
				auto primaryName = scene.GameObjectName(node);
				auto overrideCommit = MakePrefabOverrideCommit(draft.get(), &scene, selectedObjects);
				if (!overrideCommit)
				{
					diagnostic = overrideCommit.error;
				}
				auto groupProperties =
				    DrawGameObjectGroupProperties(*objectGroup, primaryName ? primaryName.value : String{}, diagnostic,
				                                  overrideCommit ? overrideCommit.value : GroupCommitObserver{});
				if (!groupProperties)
				{
					diagnostic = groupProperties.error;
				}
				else
				{
					changed |= groupProperties.value;
				}
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
				if (selectedObjects.size() > 1)
				{
					auto summary =
					    DrawCommonComponentSummary(selectedObjects, node, registry, componentEditors, inspectorContext,
					                               loadContext, workspace, scriptTypes, &scene, draft.get());
					diagnostic = summary ? String{} : summary.error;
					changed |= summary && summary.value;
				}
				else
				{
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
						const ComponentEditor* customEditor =
						    componentEditors.Find(scriptHeader ? scriptHeader->TypeName()
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
					changed |= DrawAddComponentBrowser(componentEditors, inspectorContext, diagnostic, scriptTypes,
					                                   selectedObjects);
				}
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
                               ProjectWorkspace* workspace, ResourceSession* resources,
                               EditorSelectionContext* selection, ResourceId documentId)
{
	(void)selection;
	(void)documentId;
	if (inspectorOpen && !*inspectorOpen)
	{
		return;
	}
	ImGui::Begin("Inspector", inspectorOpen);
	Icons::DrawPanelContextMenu("RuntimeInspectorPanelActions", inspectorOpen);
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
