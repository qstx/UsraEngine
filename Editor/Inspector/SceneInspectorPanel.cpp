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
#include <Scene/EditorGizmo.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Scene/RenderingComponents.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <tuple>

namespace Ursa::Editor
{
namespace
{
using SerializedJson = nlohmann::json;
std::optional<SerializedJson> copiedComponentPayload;
using GroupCommitObserver =
    std::function<Result<void>(const SerializedObjectGroup&, const String&, const Vector<bool>&)>;

constexpr Float32 DegreesToRadians = 0.01745329251994329577f;
constexpr Float32 RadiansToDegrees = 57.295779513082320876f;

Float32 PositiveDegrees(Float32 radians)
{
	Float32 degrees = std::fmod(radians * RadiansToDegrees, 360.f);
	return degrees < 0.f ? degrees + 360.f : degrees;
}

Vec3 UnityEulerAngles(Quat rotation)
{
	const Float32 lengthSquared =
	    rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
	if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f)
	{
		return {};
	}
	const Float32 inverseLength = 1.f / std::sqrt(lengthSquared);
	rotation.x *= inverseLength;
	rotation.y *= inverseLength;
	rotation.z *= inverseLength;
	rotation.w *= inverseLength;
	const Float32 r13 = 2.f * (rotation.x * rotation.z + rotation.w * rotation.y);
	const Float32 r23 = 2.f * (rotation.y * rotation.z - rotation.w * rotation.x);
	const Float32 r21 = 2.f * (rotation.x * rotation.y + rotation.w * rotation.z);
	const Float32 r22 = 1.f - 2.f * (rotation.x * rotation.x + rotation.z * rotation.z);
	const Float32 r31 = 2.f * (rotation.x * rotation.z - rotation.w * rotation.y);
	const Float32 r11 = 1.f - 2.f * (rotation.y * rotation.y + rotation.z * rotation.z);
	const Float32 x = std::asin(std::clamp(-r23, -1.f, 1.f));
	const Float32 cosineX = std::cos(x);
	const Float32 y = std::abs(cosineX) > 1.0e-5f
	                      ? std::atan2(r13, 1.f - 2.f * (rotation.x * rotation.x + rotation.y * rotation.y))
	                      : std::atan2(-r31, r11);
	const Float32 z = std::abs(cosineX) > 1.0e-5f ? std::atan2(r21, r22) : 0.f;
	return {PositiveDegrees(x), PositiveDegrees(y), PositiveDegrees(z)};
}

Quat UnityEulerQuaternion(Vec3 degrees)
{
	const Float32 halfX = degrees.x * DegreesToRadians * 0.5f;
	const Float32 halfY = degrees.y * DegreesToRadians * 0.5f;
	const Float32 halfZ = degrees.z * DegreesToRadians * 0.5f;
	const Float32 sx = std::sin(halfX);
	const Float32 cx = std::cos(halfX);
	const Float32 sy = std::sin(halfY);
	const Float32 cy = std::cos(halfY);
	const Float32 sz = std::sin(halfZ);
	const Float32 cz = std::cos(halfZ);
	return {cy * sx * cz + sy * cx * sz, sy * cx * cz - cy * sx * sz, cy * cx * sz - sy * sx * cz,
	        cy * cx * cz + sy * sx * sz};
}

bool DrawInspectorComponentFoldout(Icons::Icon icon, const char* label)
{
	Icons::Inline(icon);
	ImGui::SameLine();
	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
}

bool DrawCameraInspectorFields(CameraComponent& camera, String& diagnostic)
{
	Float32 fieldOfView = camera.verticalFovRadians * RadiansToDegrees;
	Float32 nearPlane = camera.nearPlane;
	Float32 farPlane = camera.farPlane;
	bool edited = ImGui::DragFloat("Field of View", &fieldOfView, 0.5f, 1.f, 179.f, "%.1f°");
	ImGui::TextUnformatted("Clipping Planes");
	ImGui::Indent();
	edited |= ImGui::DragFloat("Near", &nearPlane, 0.01f, 0.001f, 10000.f, "%.3f");
	edited |= ImGui::DragFloat("Far", &farPlane, 0.1f, 0.002f, 100000.f, "%.3f");
	ImGui::Unindent();
	if (!edited)
	{
		return false;
	}
	if (!std::isfinite(fieldOfView) || !std::isfinite(nearPlane) || !std::isfinite(farPlane) || fieldOfView <= 0.f ||
	    fieldOfView >= 180.f || nearPlane <= 0.f || farPlane <= nearPlane)
	{
		diagnostic = "Camera requires 0 < FOV < 180 and 0 < Near < Far";
		return false;
	}
	camera.verticalFovRadians = fieldOfView * DegreesToRadians;
	camera.nearPlane = nearPlane;
	camera.farPlane = farPlane;
	diagnostic.clear();
	return true;
}

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
		    const bool worldTransformEdit = field == "worldTransform";
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
		    else if (field == "layer")
		    {
			    overridePath = "/layer";
		    }
		    else if (field == "tag")
		    {
			    overridePath = "/tag";
		    }
		    else if (field == "static")
		    {
			    overridePath = "/static";
		    }
		    else if (field == "position" || field == "rotation" || field == "scale")
		    {
			    overridePath = "/localTransform/" + field;
		    }
		    else if (!worldTransformEdit)
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
			    if (worldTransformEdit)
			    {
				    for (const auto& item : {std::pair<const char*, const char*>{"/Fields/position", "/localTransform/position"},
				                            {"/Fields/rotation", "/localTransform/rotation"},
				                            {"/Fields/scale", "/localTransform/scale"}})
				    {
					    auto value = group.TargetValue(index, item.first);
					    if (!value)
					    {
						    return Result<void>::Failure(value.error);
					    }
					    overrides.push_back({targets[index]->instanceId, targets[index]->sourceObjectId, item.second,
					                        std::move(value.value)});
				    }
			    }
			    else
			    {
				    auto value = group.TargetValue(index, rootPath);
				    if (!value)
				    {
						    return Result<void>::Failure(value.error);
				    }
				    overrides.push_back(
					        {targets[index]->instanceId, targets[index]->sourceObjectId, overridePath, std::move(value.value)});
			    }
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
	auto layer = object->Layer();
	auto tag = object->Tag();
	auto isStatic = object->IsStatic();
	auto transform = object->GetLocalTransform();
	if (!name || !active || !layer || !tag || !isStatic || !transform)
	{
		return Result<SerializedJson>::Failure(!name       ? name.error
		                                       : !active   ? active.error
		                                       : !layer    ? layer.error
		                                       : !tag      ? tag.error
		                                       : !isStatic ? isStatic.error
		                                                   : transform.error);
	}
	const auto& value = transform.value;
	return Result<SerializedJson>::Success(
	    {{"Fields",
	      {{"name", ToStdString(name.value)},
	       {"active", active.value},
	       {"layer", layer.value},
	       {"tag", ToStdString(tag.value)},
	       {"static", isStatic.value},
	       {"position", {value.position.x, value.position.y, value.position.z}},
	       {"rotation", {value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w}},
	       {"scale", {value.scale.x, value.scale.y, value.scale.z}}}},
	     {"FieldTypes",
	      {{"name", {{"Kind", "String"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.String"}}},
	       {"active", {{"Kind", "Boolean"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.Boolean"}}},
	       {"layer", {{"Kind", "UnsignedInteger"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.UInt32"}}},
	       {"tag", {{"Kind", "String"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.String"}}},
	       {"static", {{"Kind", "Boolean"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.Boolean"}}},
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
		if (!fields.at("name").is_string() || !fields.at("active").is_boolean() ||
		    !fields.at("layer").is_number_unsigned() || fields.at("layer").get<UInt32>() > 31 ||
		    !fields.at("tag").is_string() || fields.at("tag").get<std::string>().empty() ||
		    fields.at("tag").get<std::string>().size() > 256 || !fields.at("static").is_boolean() ||
		    !fields.at("position").is_array() || fields.at("position").size() != 3 ||
		    !fields.at("rotation").is_array() || fields.at("rotation").size() != 4 || !fields.at("scale").is_array() ||
		    fields.at("scale").size() != 3)
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
		auto layered = object->SetLayer(fields["layer"].get<UInt32>());
		if (!layered)
		{
			return Result<SerializedJson>::Failure(layered.error);
		}
		auto tagged = object->SetTag(ToUrsaString(fields["tag"].get<std::string>()));
		if (!tagged)
		{
			return Result<SerializedJson>::Failure(tagged.error);
		}
		auto madeStatic = object->SetStatic(fields["static"].get<bool>());
		if (!madeStatic)
		{
			return Result<SerializedJson>::Failure(madeStatic.error);
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
		return fields.at("name").get<std::string>().size() <= 256 && fields.at("layer").is_number_unsigned() &&
		               fields.at("layer").get<UInt32>() <= 31 && fields.at("tag").is_string() &&
		               !fields.at("tag").get<std::string>().empty() &&
		               fields.at("tag").get<std::string>().size() <= 256 && fields.at("static").is_boolean()
		           ? Result<void>{}
		           : Result<void>::Failure("GameObject name, tag, or layer is invalid");
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

Result<bool> SetAndApplyGroupValue(SerializedObjectGroup& group, const String& path, SerializedJson value,
                                   const GroupCommitObserver& commitObserver);

Result<bool> DrawGameObjectGroupProperties(SerializedObjectGroup& group, const String& primaryName, String& diagnostic,
                                           const ProjectTagLayerSettings& tagLayerSettings,
                                           const std::function<void(bool)>& openTagLayerSettings,
                                           const GroupCommitObserver& commitObserver = {}, Scene* scene = nullptr,
                                           const Vector<GameObject::Ptr>* sceneTargets = nullptr,
                                           bool allowWorldTransform = false)
{
	if (group.TargetCount() > 1)
	{
		ImGui::Text("%zu objects selected · %s is active", group.TargetCount(), primaryName.c_str());
		ImGui::TextDisabled("Scene Gizmo edits all selected objects; the active object sets the Pivot handle.");
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
	const auto active = group.DescribeProperty("/Fields/active");
	if (!active)
	{
		return Result<bool>::Failure(active.error);
	}
	const auto staticState = group.DescribeProperty("/Fields/static");
	if (!staticState)
	{
		return Result<bool>::Failure(staticState.error);
	}
	bool activeValue = active.value.at("Value").get<bool>();
	Icons::Inline(Icons::Icon::GameObject);
	ImGui::SameLine();
	ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, active.value.value("HasMultipleDifferentValues", false));
	const bool activeChanged = ImGui::Checkbox("##GameObjectActive", &activeValue);
	ImGui::PopItemFlag();
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Active");
	}
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
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - 52.0f);
	ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, name.value.value("HasMultipleDifferentValues", false));
	const bool nameChanged = ImGui::InputText("##GameObjectName", nameBuffer, sizeof(nameBuffer));
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
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight() - 48.0f);
	ImGui::TextUnformatted("Static");
	ImGui::SameLine();
	bool isStatic = staticState.value.at("Value").get<bool>();
	ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, staticState.value.value("HasMultipleDifferentValues", false));
	const bool staticChanged = ImGui::Checkbox("##GameObjectStatic", &isStatic);
	ImGui::PopItemFlag();
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Static");
	}
	if (staticChanged)
	{
		auto set = group.SetValue("/Fields/static", isStatic);
		auto applied = set ? apply(ToUrsaString("/Fields/static")) : Result<void>::Failure(set.error);
		if (!applied)
		{
			diagnostic = applied.error;
			return Result<bool>::Failure(diagnostic);
		}
		changed = true;
	}
	const auto tag = group.DescribeProperty("/Fields/tag");
	if (!tag)
	{
		return Result<bool>::Failure(tag.error);
	}
	String tagValue = ToUrsaString(tag.value.at("Value").get<std::string>());
	const Float32 tagLayerWidth = std::max(60.f, (ImGui::GetContentRegionAvail().x - 72.f) * 0.5f);
	ImGui::SetNextItemWidth(tagLayerWidth);
	ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, tag.value.value("HasMultipleDifferentValues", false));
	const char* selectedTag = tag.value.value("HasMultipleDifferentValues", false) ? "<Mixed>" : tagValue.c_str();
	bool tagChanged = false;
	if (ImGui::BeginCombo("Tag", selectedTag))
	{
		for (const auto& candidate : tagLayerSettings.tags)
		{
			if (ImGui::Selectable(candidate.c_str(),
			                      !tag.value.value("HasMultipleDifferentValues", false) && candidate == tagValue))
			{
				tagValue = candidate;
				tagChanged = true;
			}
		}
		if (openTagLayerSettings)
		{
			ImGui::Separator();
			if (Icons::MenuItem("Add Tag..."))
			{
				openTagLayerSettings(false);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::PopItemFlag();
	if (tagChanged)
	{
		auto set = group.SetValue("/Fields/tag", ToStdString(tagValue));
		auto applied = set ? apply(ToUrsaString("/Fields/tag")) : Result<void>::Failure(set.error);
		if (!applied)
		{
			diagnostic = applied.error;
			return Result<bool>::Failure(diagnostic);
		}
		changed = true;
	}
	Icons::SameLine();
	const auto layer = group.DescribeProperty("/Fields/layer");
	if (!layer)
	{
		return Result<bool>::Failure(layer.error);
	}
	const UInt32 layerValue = layer.value.at("Value").get<UInt32>();
	const bool mixedLayer = layer.value.value("HasMultipleDifferentValues", false);
	const auto layerLabel = [&tagLayerSettings](UInt32 value)
	{
		const String& name = tagLayerSettings.layers[value];
		return name.empty() ? String("Layer ") + ToUrsaString(std::to_string(value)) : name;
	};
	const String selectedLayerLabel = mixedLayer ? String("<Mixed>") : layerLabel(layerValue);
	ImGui::SetNextItemWidth(tagLayerWidth);
	String layerError;
	if (ImGui::BeginCombo("Layer", selectedLayerLabel.c_str()))
	{
		for (UInt32 candidate = 0; candidate < 32; ++candidate)
		{
			const String label = layerLabel(candidate);
			ImGui::PushID(static_cast<int>(candidate));
			if (ImGui::Selectable(label.c_str(), !mixedLayer && candidate == layerValue))
			{
				auto set = group.SetValue("/Fields/layer", candidate);
				auto applied = set ? apply(ToUrsaString("/Fields/layer")) : Result<void>::Failure(set.error);
				if (!applied)
				{
					layerError = applied.error;
				}
				else
				{
					changed = true;
				}
			}
			ImGui::PopID();
		}
		if (openTagLayerSettings)
		{
			ImGui::Separator();
			if (Icons::MenuItem("Add Layer..."))
			{
				openTagLayerSettings(true);
			}
		}
		ImGui::EndCombo();
	}
	if (!layerError.empty())
	{
		diagnostic = layerError;
		return Result<bool>::Failure(diagnostic);
	}
	const auto drawVector = [&](const char* label, const char* path, int componentCount, float speed, float minimum,
	                            float maximum) -> Result<bool>
	{
		auto state = group.DescribeProperty(path);
		if (!state)
		{
			return Result<bool>::Failure(state.error);
		}
		const auto& current = state.value.at("Value");
		static constexpr const char* axisLabels[]{"X", "Y", "Z", "W"};
		static constexpr ImVec4 axisColors[]{{0.92f, 0.38f, 0.35f, 1.f},
		                                     {0.48f, 0.78f, 0.42f, 1.f},
		                                     {0.35f, 0.62f, 0.94f, 1.f},
		                                     {0.75f, 0.75f, 0.75f, 1.f}};
		bool changed = false;
		ImGui::PushID(label);
		if (ImGui::BeginTable("##TransformVector", componentCount + 1,
		                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, 76.f);
			for (int index = 0; index < componentCount; ++index)
			{
				ImGui::TableSetupColumn(axisLabels[index], ImGuiTableColumnFlags_WidthStretch);
			}
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			for (int index = 0; index < componentCount; ++index)
			{
				ImGui::TableSetColumnIndex(index + 1);
				ImGui::PushID(index);
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(axisColors[index], "%s", axisLabels[index]);
				ImGui::SameLine(0.f, 4.f);
				const String componentPath = String(path) + "/" + ToUrsaString(std::to_string(index));
				auto component = group.DescribeProperty(componentPath);
				if (!component)
				{
					ImGui::PopID();
					ImGui::EndTable();
					ImGui::PopID();
					return Result<bool>::Failure(component.error);
				}
				Float32 value = current.at(index).get<Float32>();
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,
				                    component.value.value("HasMultipleDifferentValues", false));
				ImGui::SetNextItemWidth(-FLT_MIN);
				const bool edited = ImGui::DragFloat("##Value", &value, speed, minimum, maximum, "%.3f");
				ImGui::PopItemFlag();
				if (edited && value != current.at(index).get<Float32>())
				{
					auto set = group.SetValue(componentPath, value);
					if (!set)
					{
						ImGui::PopID();
						ImGui::EndTable();
						ImGui::PopID();
						return Result<bool>::Failure(set.error);
					}
					auto applied = apply(ToUrsaString(path));
					if (!applied)
					{
						ImGui::PopID();
						ImGui::EndTable();
						ImGui::PopID();
						return Result<bool>::Failure(applied.error);
					}
					changed = true;
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::PopID();
		return Result<bool>::Success(changed);
	};
	const auto drawRotation = [&]() -> Result<bool>
	{
		auto state = group.DescribeProperty("/Fields/rotation");
		if (!state)
		{
			return Result<bool>::Failure(state.error);
		}
		const auto& current = state.value.at("Value");
		const Vec3 euler = UnityEulerAngles({current.at(0).get<Float32>(), current.at(1).get<Float32>(),
		                                     current.at(2).get<Float32>(), current.at(3).get<Float32>()});
		Float32 values[] = {euler.x, euler.y, euler.z};
		static constexpr const char* axisLabels[]{"X", "Y", "Z"};
		static constexpr ImVec4 axisColors[]{{0.92f, 0.38f, 0.35f, 1.f},
		                                     {0.48f, 0.78f, 0.42f, 1.f},
		                                     {0.35f, 0.62f, 0.94f, 1.f}};
		Int32 editedAxis = -1;
		ImGui::PushID("Rotation");
		if (ImGui::BeginTable("##TransformRotation", 4,
		                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, 76.f);
			for (int index = 0; index < 3; ++index)
			{
				ImGui::TableSetupColumn(axisLabels[index], ImGuiTableColumnFlags_WidthStretch);
			}
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Rotation");
			ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,
			                    state.value.value("HasMultipleDifferentValues", false));
			for (int index = 0; index < 3; ++index)
			{
				ImGui::TableSetColumnIndex(index + 1);
				ImGui::PushID(index);
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(axisColors[index], "%s", axisLabels[index]);
				ImGui::SameLine(0.f, 4.f);
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::DragFloat("##Value", &values[index], 0.25f, 0.f, 0.f, "%.1f°"))
				{
					editedAxis = index;
				}
				ImGui::PopID();
			}
			ImGui::PopItemFlag();
			ImGui::EndTable();
		}
		ImGui::PopID();
		if (editedAxis < 0 || values[editedAxis] == (editedAxis == 0 ? euler.x : editedAxis == 1 ? euler.y : euler.z))
		{
			return Result<bool>::Success(false);
		}
		const Quat rotation = UnityEulerQuaternion({values[0], values[1], values[2]});
		const Float32 components[] = {rotation.x, rotation.y, rotation.z, rotation.w};
		for (int index = 0; index < 4; ++index)
		{
			auto set =
			    group.SetValue(String("/Fields/rotation/") + ToUrsaString(std::to_string(index)), components[index]);
			if (!set)
			{
				return Result<bool>::Failure(set.error);
			}
		}
		auto applied = apply(ToUrsaString("/Fields/rotation"));
		return applied ? Result<bool>::Success(true) : Result<bool>::Failure(applied.error);
	};
	Icons::Inline(Icons::Icon::Move);
	ImGui::SameLine();
	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	const bool transformOpen = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
	if (Icons::Button("TransformActions", Icons::Icon::More, "Transform actions"))
	{
		ImGui::OpenPopup("TransformActions");
	}
	static std::optional<SerializedJson> copiedPosition;
	static std::optional<SerializedJson> copiedRotation;
	static std::optional<SerializedJson> copiedScale;
	static std::optional<Mat4> copiedWorldTransform;
	bool resetTransform = false;
	if (ImGui::BeginPopup("TransformActions"))
	{
		const auto copyValue = [&](const char* path, std::optional<SerializedJson>& clipboard)
		{
			auto value = group.DescribeProperty(path);
			if (value)
			{
				clipboard = value.value.at("Value");
			}
		};
		if (Icons::BeginMenu("Copy"))
		{
			if (Icons::MenuItem("Position"))
			{
				copyValue("/Fields/position", copiedPosition);
			}
			if (Icons::MenuItem("Rotation"))
			{
				copyValue("/Fields/rotation", copiedRotation);
			}
			if (Icons::MenuItem("Scale"))
			{
				copyValue("/Fields/scale", copiedScale);
			}
			if (scene && sceneTargets && !sceneTargets->empty())
			{
				if (Icons::MenuItem("World Transform"))
				{
					auto world = scene->WorldTransform(sceneTargets->front());
					if (world)
					{
						copiedWorldTransform = world.value;
					}
					else
					{
						diagnostic = world.error;
					}
				}
			}
			ImGui::EndMenu();
		}
		if (Icons::BeginMenu("Paste"))
		{
			const auto pasteValue = [&](const char* label, const char* path,
			                            const std::optional<SerializedJson>& clipboard)
			{
				ImGui::BeginDisabled(!clipboard.has_value());
				if (Icons::MenuItem(label))
				{
					auto applied = SetAndApplyGroupValue(group, path, *clipboard, commitObserver);
					if (!applied)
					{
						diagnostic = applied.error;
					}
					else
					{
						changed |= applied.value;
					}
				}
				ImGui::EndDisabled();
			};
			pasteValue("Position", "/Fields/position", copiedPosition);
			pasteValue("Rotation", "/Fields/rotation", copiedRotation);
			pasteValue("Scale", "/Fields/scale", copiedScale);
			if (scene && sceneTargets && allowWorldTransform)
			{
				ImGui::BeginDisabled(!copiedWorldTransform.has_value());
				if (Icons::MenuItem("World Transform"))
				{
					Vector<LocalTransform> originals;
					originals.reserve(sceneTargets->size());
					for (const auto& target : *sceneTargets)
					{
						auto local = scene->GetLocalTransform(target);
						if (!local)
						{
							diagnostic = local.error;
							break;
						}
						originals.push_back(local.value);
					}
					Size appliedCount = 0;
					if (diagnostic.empty())
					{
						for (; appliedCount < sceneTargets->size(); ++appliedCount)
						{
							auto applied = ApplyGizmoWorldTransform(*scene, (*sceneTargets)[appliedCount],
							                                       *copiedWorldTransform);
							if (!applied)
							{
								diagnostic = applied.error;
								break;
							}
						}
					}
					if (!diagnostic.empty())
					{
						while (appliedCount > 0)
						{
							--appliedCount;
							scene->SetLocalTransform((*sceneTargets)[appliedCount], originals[appliedCount]);
						}
					}
					else
					{
						for (Size index = 0; index < sceneTargets->size() && diagnostic.empty(); ++index)
						{
							auto local = scene->GetLocalTransform((*sceneTargets)[index]);
							if (!local)
							{
								diagnostic = local.error;
								break;
							}
							for (const auto& item : {
							         std::pair<const char*, SerializedJson>{"/Fields/position", {local.value.position.x,
							                                                            local.value.position.y,
							                                                            local.value.position.z}},
							         {"/Fields/rotation", {local.value.rotation.x, local.value.rotation.y,
							                              local.value.rotation.z, local.value.rotation.w}},
							         {"/Fields/scale", {local.value.scale.x, local.value.scale.y, local.value.scale.z}}})
							{
								auto set = group.SetValue(item.first, item.second);
								if (!set)
								{
									diagnostic = set.error;
									break;
								}
							}
						}
						if (diagnostic.empty())
						{
							const auto pendingChanges = group.PendingTargetChanges();
							const bool hasChanges =
							    std::any_of(pendingChanges.begin(), pendingChanges.end(), [](bool value) { return value; });
							auto committed = apply(ToUrsaString("/Fields/worldTransform"));
							if (!committed)
							{
								diagnostic = committed.error;
							}
							else
							{
								changed |= hasChanges;
							}
						}
						if (!diagnostic.empty())
						{
							for (Size index = 0; index < originals.size(); ++index)
							{
								scene->SetLocalTransform((*sceneTargets)[index], originals[index]);
							}
						}
					}
				}
				ImGui::EndDisabled();
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		resetTransform = Icons::MenuItem("Reset");
		ImGui::EndPopup();
	}
	if (!diagnostic.empty())
	{
		return Result<bool>::Failure(diagnostic);
	}
	if (resetTransform)
	{
		for (const auto& item : {std::pair<String, SerializedJson>{"/Fields/position", {0.f, 0.f, 0.f}},
		                         {"/Fields/rotation", {0.f, 0.f, 0.f, 1.f}},
		                         {"/Fields/scale", {1.f, 1.f, 1.f}}})
		{
			auto reset = SetAndApplyGroupValue(group, item.first, item.second, commitObserver);
			if (!reset)
			{
				return Result<bool>::Failure(reset.error);
			}
			changed |= reset.value;
		}
	}
	if (transformOpen)
	{
		auto position = drawVector("Position", "/Fields/position", 3, 0.05f, 0.0f, 0.0f);
		if (!position)
		{
			return position;
		}
		changed |= position.value;
		auto rotation = drawRotation();
		if (!rotation)
		{
			return rotation;
		}
		changed |= rotation.value;
		auto scale = drawVector("Scale", "/Fields/scale", 3, 0.05f, 0.0f, 0.0f);
		if (!scale)
		{
			return scale;
		}
		changed |= scale.value;
	}
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
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}
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

Result<Component*> PasteComponentAsNew(ComponentEditors& componentEditors, const String& type,
                                       const SerializedJson& payload, ComponentInspectorContext& context,
                                       ComponentRegistry& registry, ComponentLoadContext& loadContext, Scene* scene)
{
	if (!payload.is_object() || !payload.contains("fields") || !payload["fields"].is_object())
	{
		return Result<Component*>::Failure("Copied component data has no serialized fields");
	}
	const String factoryType = type.compare(0, 7, "native:") == 0 || type.compare(0, 7, "script:") == 0
	                               ? type.substr(7)
	                               : type;
	auto created = componentEditors.Create(factoryType, context);
	if (!created)
	{
		return created;
	}
	Component* component = created.value;
	const auto rollback = [&]()
	{
		if (scene)
		{
			auto id = scene->ComponentByPersistentId(component->PersistentId());
			if (id)
			{
				scene->RemoveComponent(id.value);
			}
		}
		else if (const auto owner = component->GetGameObject())
		{
			owner->RemoveComponent(component);
		}
	};
	ComponentSaveContext saveContext;
	saveContext.scene = scene;
	saveContext.serialization = &loadContext;
	auto serialized = SerializedObject::Create(*component, registry, loadContext, saveContext);
	if (!serialized)
	{
		rollback();
		return Result<Component*>::Failure(serialized.error);
	}
	Vector<SerializedObject*> targets{serialized.value.get()};
	auto group = SerializedObjectGroup::Create(std::move(targets));
	if (!group)
	{
		rollback();
		return Result<Component*>::Failure(group.error);
	}
	const auto current = group.value.TargetSnapshot(0);
	if (!current || !current.value.contains("Fields") || !current.value["Fields"].is_object())
	{
		rollback();
		return Result<Component*>::Failure(current ? "New component has no writable serialized fields" : current.error);
	}
	for (auto field = payload["fields"].begin(); field != payload["fields"].end(); ++field)
	{
		if (!current.value["Fields"].contains(field.key()))
		{
			continue;
		}
		String escapedField = ToUrsaString(field.key());
		for (Size offset = 0; (offset = escapedField.find('~', offset)) != String::npos; offset += 2)
		{
			escapedField.replace(offset, 1, "~0");
		}
		for (Size offset = 0; (offset = escapedField.find('/', offset)) != String::npos; offset += 2)
		{
			escapedField.replace(offset, 1, "~1");
		}
		auto set = group.value.SetValue("/Fields/" + escapedField, field.value());
		if (!set)
		{
			rollback();
			return Result<Component*>::Failure(set.error);
		}
	}
	auto applied = group.value.Apply();
	if (!applied)
	{
		rollback();
		return Result<Component*>::Failure(applied.error);
	}
	return Result<Component*>::Success(component);
}

Result<bool> ResetComponentToDefaults(Component& component, const String& type, ComponentEditors& componentEditors,
                                      ComponentInspectorContext& context, ComponentRegistry& registry,
                                      ComponentLoadContext& loadContext, SceneDraft* draft, Scene* scene)
{
	const auto* descriptor = componentEditors.FindType(type);
	if (!descriptor || !component.GetGameObject())
	{
		return Result<bool>::Failure("This component has no registered default factory");
	}

	const auto defaultObject = GameObject::Create("Default Component Values");
	ComponentInspectorContext defaultsContext = context;
	defaultsContext.object = defaultObject;
	defaultsContext.roots = {defaultObject};
	defaultsContext.targets.clear();
	defaultsContext.primaryIndex = 0;
	defaultsContext.scene = nullptr;
	defaultsContext.selection = nullptr;
	defaultsContext.prefab = nullptr;
	defaultsContext.documentId = {};
	auto defaultsComponent = descriptor->create(defaultsContext);
	if (!defaultsComponent || !defaultsComponent.value)
	{
		return Result<bool>::Failure(defaultsComponent ? "Component factory returned no default component"
		                                              : defaultsComponent.error);
	}

	SerializedJson defaultFields;
	const bool defaultEnabled = defaultsComponent.value->IsEnabled();
	{
		ComponentSaveContext saveContext;
		saveContext.serialization = &loadContext;
		auto serialized = SerializedObject::Create(*defaultsComponent.value, registry, loadContext, saveContext);
		if (!serialized)
		{
			return Result<bool>::Failure(serialized.error);
		}
		const auto snapshot = serialized.value->Snapshot();
		if (!snapshot.contains("Fields") || !snapshot.at("Fields").is_object())
		{
			return Result<bool>::Failure("Default component has no serialized fields");
		}
		defaultFields = snapshot.at("Fields");
	}
	ComponentSaveContext saveContext;
	saveContext.scene = scene;
	saveContext.serialization = &loadContext;
	auto serialized = SerializedObject::Create(component, registry, loadContext, saveContext);
	if (!serialized)
	{
		return Result<bool>::Failure(serialized.error);
	}
	Vector<SerializedObject*> targets{serialized.value.get()};
	auto group = SerializedObjectGroup::Create(std::move(targets));
	if (!group)
	{
		return Result<bool>::Failure(group.error);
	}
	const auto current = group.value.TargetSnapshot(0);
	if (!current || !current.value.contains("Fields") || !current.value.at("Fields").is_object())
	{
		return Result<bool>::Failure(current ? "Component has no serialized fields" : current.error);
	}

	Vector<String> changedPaths;
	for (auto field = defaultFields.begin(); field != defaultFields.end(); ++field)
	{
		if (!current.value.at("Fields").contains(field.key()))
		{
			continue;
		}
		String escapedField = ToUrsaString(field.key());
		for (Size offset = 0; (offset = escapedField.find('~', offset)) != String::npos; offset += 2)
		{
			escapedField.replace(offset, 1, "~0");
		}
		for (Size offset = 0; (offset = escapedField.find('/', offset)) != String::npos; offset += 2)
		{
			escapedField.replace(offset, 1, "~1");
		}
		const String path = "/Fields/" + escapedField;
		auto set = group.value.SetValue(path, field.value());
		if (!set)
		{
			return Result<bool>::Failure(set.error);
		}
		changedPaths.push_back(path);
	}
	const auto changedTargets = group.value.PendingTargetChanges();
	auto applied = group.value.Apply();
	if (!applied)
	{
		return Result<bool>::Failure(applied.error);
	}
	const bool enabledChanged = component.SupportsEnabled() && component.IsEnabled() != defaultEnabled;
	if (enabledChanged)
	{
		auto enabled = component.SetEnabled(defaultEnabled);
		if (!enabled)
		{
			return Result<bool>::Failure(enabled.error);
		}
	}
	if (!changedPaths.empty())
	{
		Vector<Component*> componentTargets{&component};
		auto commit = MakePrefabOverrideCommit(draft, scene, {}, &componentTargets);
		if (!commit)
		{
			return Result<bool>::Failure(commit.error);
		}
		for (const String& path : changedPaths)
		{
			auto committed = commit.value(group.value, path, changedTargets);
			if (!committed)
			{
				return Result<bool>::Failure(committed.error);
			}
		}
	}
	return Result<bool>::Success(enabledChanged ||
	                             std::any_of(changedTargets.begin(), changedTargets.end(), [](bool value) { return value; }));
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
                                   EditorSelectionContext* selection, ResourceId parentOverride = {},
                                   const PrefabAsset* prefabSnapshot = nullptr,
                                   std::shared_ptr<PrefabAsset>* capturedSnapshot = nullptr, bool captureOnly = false,
                                   Vector<Mat4>* capturedWorldTransforms = nullptr,
                                   const Vector<Mat4>* sourceWorldTransforms = nullptr)
{
	struct Target
	{
		GameObject::Ptr object;
		GameObject::Ptr parent;
		Ursa::Size siblingIndex;
		bool append = false;
	};

	Vector<Target> targets;
	GameObject::Ptr pasteParent;
	if (parentOverride)
	{
		auto parent = scene.GameObjectById(parentOverride);
		if (!parent)
		{
			return Result<void>::Failure(parent.error);
		}
		pasteParent = parent.value;
	}
	Vector<GameObject::Ptr> roots;
	Ursa::Size primaryIndex = 0;
	if (prefabSnapshot)
	{
		if (!prefabSnapshot->Root())
		{
			return Result<void>::Failure("The copied GameObject snapshot is empty");
		}
		roots = prefabSnapshot->Root()->Children();
		if (roots.empty())
		{
			return Result<void>::Failure("The copied GameObject snapshot has no root objects");
		}
		GameObject::Ptr anchor;
		if (!pasteParent && selection && !selection->entries.empty())
		{
			const auto* entry = selection->PrimaryEntry();
			if (entry && entry->kind == EditorSelectionKind::GameObject && entry->documentId == selection->activeDocumentId &&
			    !entry->playSession && !entry->IsMissing())
			{
				auto selectedObject = scene.GameObjectById(entry->objectId);
				if (selectedObject)
				{
					anchor = selectedObject.value;
				}
			}
		}
		GameObject::Ptr anchorParent;
		Ursa::Size anchorIndex = 0;
		if (anchor)
		{
			auto parent = scene.ParentOf(anchor);
			if (!parent)
			{
				return Result<void>::Failure(parent.error);
			}
			anchorParent = parent.value;
			auto siblings = anchorParent ? scene.ChildrenOf(anchorParent)
			                            : Result<Vector<GameObject::Ptr>>::Success(scene.RootGameObjects());
			if (!siblings)
			{
				return Result<void>::Failure(siblings.error);
			}
			auto position = std::find(siblings.value.begin(), siblings.value.end(), anchor);
			if (position == siblings.value.end())
			{
				return Result<void>::Failure("The paste target is no longer in its sibling list");
			}
			anchorIndex = static_cast<Ursa::Size>(position - siblings.value.begin());
		}
		for (Ursa::Size index = 0; index < roots.size(); ++index)
		{
			targets.push_back({{}, pasteParent ? pasteParent : anchorParent, anchorIndex, !anchor && !pasteParent});
			if (roots[index]->Id() == primaryId)
			{
				primaryIndex = index;
			}
		}
	}
	else
	{
		Set<String> selected;
		for (const auto& id : selectedIds)
		{
			if (id)
			{
				selected.insert(id.ToString());
			}
		}
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
			targets.push_back(
			    {object.value, parent.value, static_cast<Ursa::Size>(position - siblings.value.begin()), false});
		}
		if (targets.empty())
		{
			return Result<void>::Failure("Select at least one editable GameObject to duplicate");
		}
		for (Ursa::Size index = 0; index < targets.size(); ++index)
		{
			roots.push_back(targets[index].object);
			if (targets[index].object->Id() == primaryId)
			{
				primaryIndex = index;
			}
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
	Result<PrefabAsset> capturedPrefab;
	const PrefabAsset* prefab = prefabSnapshot;
	if (!prefab)
	{
		capturedPrefab = CapturePrefabAsset(ResourceId::Generate(), scene, roots, registry, context, materialId);
		if (!capturedPrefab)
		{
			return Result<void>::Failure(capturedPrefab.error);
		}
		prefab = &capturedPrefab.value;
	}
	if (!prefab->Root() || prefab->Root()->Children().size() != targets.size())
	{
		return Result<void>::Failure("The copied GameObject graph no longer matches the selection");
	}
	if (captureOnly)
	{
		if (!capturedSnapshot)
		{
			return Result<void>::Failure("No destination was provided for the copied GameObject snapshot");
		}
		*capturedSnapshot = std::make_shared<PrefabAsset>(*prefab);
		if (capturedWorldTransforms)
		{
			capturedWorldTransforms->clear();
			capturedWorldTransforms->reserve(roots.size());
			for (const GameObject::Ptr& root : roots)
			{
				auto world = scene.WorldTransform(root);
				if (!world)
				{
					capturedWorldTransforms->clear();
					return Result<void>::Failure(world.error);
				}
				capturedWorldTransforms->push_back(world.value);
			}
		}
		return {};
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
	auto wrapper = prefab->Instantiate(scene, registry, context, materialId, true);
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
	if (sourceWorldTransforms && sourceWorldTransforms->size() != copies.value.size())
	{
		rollback();
		return Result<void>::Failure("The copied GameObject transforms do not match the clipboard snapshot");
	}
	Map<String, Ursa::Size> insertedByParent;
	Map<String, Ursa::Size> appendStartIndexByParent;
	for (const Target& target : targets)
	{
		const GameObject::Ptr targetParent = pasteParent ? pasteParent : target.parent;
		if (pasteParent || target.append)
		{
			const String parentKey = targetParent ? targetParent->Id().ToString() : String{};
			if (appendStartIndexByParent.find(parentKey) == appendStartIndexByParent.end())
			{
				auto siblings = targetParent ? scene.ChildrenOf(targetParent)
				                            : Result<Vector<GameObject::Ptr>>::Success(scene.RootGameObjects());
				if (!siblings)
				{
					rollback();
					return Result<void>::Failure(siblings.error);
				}
				appendStartIndexByParent.emplace(parentKey, siblings.value.size());
			}
		}
	}
	for (Ursa::Size index = 0; index < copies.value.size(); ++index)
	{
		const GameObject::Ptr targetParent = pasteParent ? pasteParent : targets[index].parent;
		auto parented = scene.SetParent(copies.value[index], targetParent);
		if (!parented)
		{
			rollback();
			return parented;
		}
		if (sourceWorldTransforms)
		{
			auto transformed = SetSceneGameObjectWorldTransform(scene, copies.value[index], (*sourceWorldTransforms)[index]);
			if (!transformed)
			{
				rollback();
				return transformed;
			}
		}
		const String parentKey = targetParent ? targetParent->Id().ToString() : String{};
		const Ursa::Size inserted = insertedByParent[parentKey]++;
		Ursa::Size insertionIndex = targets[index].siblingIndex + 1 + inserted;
		if (pasteParent || targets[index].append)
		{
			insertionIndex = appendStartIndexByParent[parentKey] + inserted;
		}
		auto reordered = scene.ReorderGameObject(copies.value[index], insertionIndex);
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

Result<void> DuplicateGameObjectSelection(Scene& scene, const Vector<ResourceId>& selectedIds, ResourceId primaryId,
                                          ComponentRegistry& registry, EditorDocuments& documents, SceneDraft& draft,
                                          EditorSelectionContext* selection, ResourceId parentOverride,
                                          const PrefabAsset* prefabSnapshot,
                                          const Vector<Mat4>* sourceWorldTransforms)
{
	return DuplicateSceneObjects(scene, selectedIds, primaryId, registry, documents, draft, selection, parentOverride,
	                             prefabSnapshot, nullptr, false, nullptr, sourceWorldTransforms);
}

Result<std::shared_ptr<PrefabAsset>> CaptureGameObjectSelection(Scene& scene, const Vector<ResourceId>& selectedIds,
                                                                ResourceId primaryId, ComponentRegistry& registry,
                                                                EditorDocuments& documents, SceneDraft& draft,
                                                                Vector<Mat4>* sourceWorldTransforms)
{
	std::shared_ptr<PrefabAsset> snapshot;
	auto captured = DuplicateSceneObjects(scene, selectedIds, primaryId, registry, documents, draft, nullptr, {}, nullptr,
	                                     &snapshot, true, sourceWorldTransforms);
	return captured ? Result<std::shared_ptr<PrefabAsset>>::Success(std::move(snapshot))
	                : Result<std::shared_ptr<PrefabAsset>>::Failure(captured.error);
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
                                ProjectWorkspace& workspace, const ProjectTagLayerSettings& tagLayerSettings,
                                ComponentRegistry& registry, ComponentEditors& componentEditors, String& diagnostic,
                                const Vector<ScriptTypeDescriptor>* scriptTypes, bool* hierarchyOpen,
                                bool* inspectorOpen, bool drawInspector, EditorSelectionContext* selection,
                                EditorHierarchy::AssetDropAction assetDrop,
                                std::function<void(bool)> openTagLayerSettings, bool canCopyGameObjects,
                                bool canPasteGameObjectsAsChild,
                                EditorHierarchy::CopySelectionAction copyGameObjects,
                                EditorHierarchy::CutSelectionAction cutGameObjects,
                                EditorHierarchy::PasteAsChildAction pasteGameObjectsAsChild)
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
				auto objectName = object->Name();
				auto groupProperties = DrawGameObjectGroupProperties(
				    group.value, objectName ? objectName.value : String{}, diagnostic, tagLayerSettings,
				    openTagLayerSettings);
				if (!groupProperties)
				{
					diagnostic = groupProperties.error;
				}
				else
				{
					changed |= groupProperties.value;
				}
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
					const auto prefabComponents = object->Components();
					for (Size componentIndex = 0; componentIndex < prefabComponents.size(); ++componentIndex)
					{
						Component* component = prefabComponents[componentIndex];
						ImGui::PushID(component->PersistentId().ToString().c_str());
						auto codec = registry.FindFor(*component);
						const auto* scriptHeader = dynamic_cast<Scripting::ScriptBehaviourComponent*>(component);
						const auto* descriptor = scriptHeader ? componentEditors.FindType(scriptHeader->TypeName())
						                         : codec      ? componentEditors.FindType(codec.value->type)
						                                      : nullptr;
						const String componentLabel = descriptor     ? descriptor->displayName
						                              : scriptHeader ? scriptHeader->TypeName()
						                              : codec        ? codec.value->type
						                                             : String("Component");
						if (component->SupportsEnabled())
						{
							bool componentEnabled = component->IsEnabled();
							if (ImGui::Checkbox("##ComponentEnabled", &componentEnabled))
							{
								auto set = component->SetEnabled(componentEnabled);
								diagnostic = set ? String{} : set.error;
								changed |= static_cast<bool>(set);
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::SetTooltip("Enabled");
							}
							ImGui::SameLine();
						}
						const bool componentOpen = DrawInspectorComponentFoldout(
						    Icons::ForComponent(component->SerializationType(), scriptHeader != nullptr),
						    componentLabel.c_str());
						const ImVec2 headerMin = ImGui::GetItemRectMin();
						const ImVec2 headerMax = ImGui::GetItemRectMax();
						bool removeFromHeader = false;
						bool resetComponent = false;
						bool moveComponentUp = false;
						bool moveComponentDown = false;
						bool copyComponent = false;
						bool pasteComponentAsNew = false;
						if (ImGui::BeginPopupContextItem("ComponentHeaderActions"))
						{
							const String factoryType = scriptHeader ? scriptHeader->TypeName() : component->SerializationType();
							ImGui::BeginDisabled(componentEditors.FindType(factoryType) == nullptr);
							resetComponent = Icons::MenuItem("Reset");
							ImGui::EndDisabled();
							ImGui::Separator();
							copyComponent = Icons::MenuItem("Copy Component");
							const String stableType = scriptHeader ? "script:" + scriptHeader->TypeName()
							                                   : "native:" + component->SerializationType();
							const bool canPasteAsNew = copiedComponentPayload && copiedComponentPayload->is_object() &&
							                          copiedComponentPayload->contains("type") &&
							                          copiedComponentPayload->contains("fields") &&
							                          copiedComponentPayload->at("fields").is_object() &&
							                          componentEditors.FindType(
							                              ToUrsaString(copiedComponentPayload->at("type").get<std::string>())
							                                  .substr(7)) &&
							                          static_cast<bool>(componentEditors.CanCreate(
							                              ToUrsaString(copiedComponentPayload->at("type").get<std::string>())
							                                  .substr(7), inspectorContext));
							ImGui::BeginDisabled(!canPasteAsNew);
							pasteComponentAsNew = Icons::MenuItem("Paste Component As New");
							ImGui::EndDisabled();
							ImGui::Separator();
							ImGui::BeginDisabled(componentIndex == 0);
							moveComponentUp = Icons::MenuItem("Move Up");
							ImGui::EndDisabled();
							ImGui::BeginDisabled(componentIndex + 1 >= prefabComponents.size());
							moveComponentDown = Icons::MenuItem("Move Down");
							ImGui::EndDisabled();
							ImGui::Separator();
							removeFromHeader = ImGui::MenuItem("Remove Component");
							ImGui::EndPopup();
						}
						if (resetComponent)
						{
							const String factoryType = scriptHeader ? scriptHeader->TypeName() : component->SerializationType();
							auto reset = ResetComponentToDefaults(*component, factoryType, componentEditors, inspectorContext,
							                                    registry, loadContext, nullptr, nullptr);
							diagnostic = reset ? String{} : reset.error;
							changed |= reset && reset.value;
							resetComponent = false;
						}
						if (copyComponent)
						{
							ComponentSaveContext saveContext;
							saveContext.serialization = &loadContext;
							auto serialized = SerializedObject::Create(*component, registry, loadContext, saveContext);
							if (!serialized || !serialized.value->Snapshot().contains("Fields"))
							{
								diagnostic = serialized ? "Component has no serialized fields" : serialized.error;
							}
							else
							{
								const String stableType = scriptHeader ? "script:" + scriptHeader->TypeName()
								                                   : "native:" + component->SerializationType();
								SerializedJson payload = SerializedJson::object();
								payload["type"] = ToStdString(stableType);
								payload["fields"] = serialized.value->Snapshot().at("Fields");
								copiedComponentPayload = std::move(payload);
							}
						}
						if (pasteComponentAsNew && copiedComponentPayload)
						{
							const String copiedStableType =
							    ToUrsaString(copiedComponentPayload->at("type").get<std::string>());
							auto pasted = PasteComponentAsNew(componentEditors, copiedStableType, *copiedComponentPayload,
							                                 inspectorContext, registry, loadContext, nullptr);
							diagnostic = pasted ? String{} : pasted.error;
							changed |= static_cast<bool>(pasted);
						}
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
						ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 28.f);
						if (Icons::Button("ComponentActions", Icons::Icon::More, "Component actions"))
						{
							ImGui::OpenPopup("PrefabComponentActions");
						}
						if (ImGui::BeginPopup("PrefabComponentActions"))
						{
							const String factoryType = scriptHeader ? scriptHeader->TypeName() : component->SerializationType();
							ImGui::BeginDisabled(componentEditors.FindType(factoryType) == nullptr);
							resetComponent = Icons::MenuItem("Reset") || resetComponent;
							ImGui::EndDisabled();
							ImGui::Separator();
							ImGui::BeginDisabled(componentIndex == 0);
							moveComponentUp = Icons::MenuItem("Move Up") || moveComponentUp;
							ImGui::EndDisabled();
							ImGui::BeginDisabled(componentIndex + 1 >= prefabComponents.size());
							moveComponentDown = Icons::MenuItem("Move Down") || moveComponentDown;
							ImGui::EndDisabled();
							ImGui::Separator();
							removeFromHeader = Icons::MenuItem("Remove Component") || removeFromHeader;
							ImGui::EndPopup();
						}
						if (resetComponent)
						{
							const String factoryType = scriptHeader ? scriptHeader->TypeName() : component->SerializationType();
							auto reset = ResetComponentToDefaults(*component, factoryType, componentEditors, inspectorContext,
							                                    registry, loadContext, nullptr, nullptr);
							diagnostic = reset ? String{} : reset.error;
							changed |= reset && reset.value;
						}
						bool reorderedComponent = false;
						const ImRect headerRect(headerMin, headerMax);
						if (headerRect.Contains(ImGui::GetIO().MousePos) &&
						    ImGui::IsMouseClicked(ImGuiMouseButton_Right))
						{
							ImGui::OpenPopup("SceneComponentActions");
						}
						if (headerRect.GetHeight() > 0.f &&
						    ImGui::BeginDragDropTargetCustom(headerRect, ImGui::GetID("##PrefabComponentOrderDropTarget")))
						{
							if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
							        editorDragSessionPayloadType, ImGuiDragDropFlags_AcceptNoDrawDefaultRect |
							                                          ImGuiDragDropFlags_AcceptBeforeDelivery))
							{
								auto decoded = DecodeEditorDragPayload(
								    payload->DataType, payload->Data, payload->DataSize,
								    selection ? std::optional<UInt64>(selection->revision) : std::nullopt);
								if (!decoded)
								{
									if (payload->IsDelivery())
									{
										diagnostic = decoded.error;
									}
								}
								else if (decoded.value.sources.size() == 1 &&
								         decoded.value.sources.front().identity.kind ==
								             EditorSelectionKind::GameObject &&
								         decoded.value.sources.front().identity.documentId == document.Id() &&
								         decoded.value.sources.front().identity.objectId == object->Id() &&
								         decoded.value.sources.front().componentId)
								{
									const bool insertAfter =
									    ImGui::GetIO().MousePos.y >= (headerMin.y + headerMax.y) * 0.5f;
									const Float32 insertionY = insertAfter ? headerMax.y : headerMin.y;
									ImGui::GetWindowDrawList()->AddLine(
									    {headerMin.x, insertionY}, {headerMax.x, insertionY},
									    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.f);
									if (payload->IsDelivery() && ClaimEditorDragDropDelivery(payload))
									{
										const auto& source = decoded.value.sources.front();
										const auto sourcePosition =
										    std::find_if(prefabComponents.begin(), prefabComponents.end(),
										                [&](const Component* candidate)
										                { return candidate->PersistentId() == source.componentId; });
										if (sourcePosition == prefabComponents.end())
										{
											diagnostic = "Dragged component is no longer attached to this Prefab GameObject.";
										}
										else
										{
											Size targetIndex = componentIndex + (insertAfter ? 1 : 0);
											const Size sourceIndex =
											    static_cast<Size>(sourcePosition - prefabComponents.begin());
											if (sourceIndex < targetIndex)
											{
												--targetIndex;
											}
											targetIndex = std::min(targetIndex, prefabComponents.size() - 1);
											if (targetIndex != sourceIndex)
											{
												auto moved = object->ReorderDetachedComponent(
												    *sourcePosition, targetIndex);
												diagnostic = moved ? String{} : moved.error;
												changed |= static_cast<bool>(moved);
												reorderedComponent = static_cast<bool>(moved);
											}
										}
									}
								}
								else if (payload->IsDelivery())
								{
									diagnostic = "Reorder one component at a time within the same Prefab GameObject.";
								}
							}
							ImGui::EndDragDropTarget();
						}
						if (moveComponentUp || moveComponentDown)
						{
							const Size destination = moveComponentUp ? componentIndex - 1 : componentIndex + 1;
						const auto moved = object->ReorderDetachedComponent(component, destination);
						diagnostic = moved ? String{} : moved.error;
						changed |= static_cast<bool>(moved);
						reorderedComponent = static_cast<bool>(moved);
					}
						if (reorderedComponent)
						{
							ImGui::PopID();
							break;
						}
						if (componentOpen)
						{
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
								changed |= DrawSerializedScriptFields(*scriptData, workspace, inspectorContext,
								                                      diagnostic, scriptTypes);
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
								changed |= DrawCameraInspectorFields(*camera, diagnostic);
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
								auto edited = (*componentEditors.Find(codec.value->type))(*component, inspectorContext,
								                                                          inspectorUi);
								diagnostic = edited ? String{} : edited.error;
								changed |= edited && edited.value;
							}
							else
							{
								ImGui::TextDisabled("No property editor registered; serialized data is preserved");
							}
						}
						if (removeFromHeader)
						{
							auto removed = object->RemoveComponent(component);
							diagnostic = removed ? String{} : removed.error;
							changed |= static_cast<bool>(removed);
						}
						ImGui::PopID();
						ImGui::Spacing();
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
	                   std::move(duplicateObjects), std::move(deleteObjects), std::move(assetDrop),
	                   document.Path().FileName(), &workspace.Index().Filesystem(), canCopyGameObjects,
	                   canPasteGameObjectsAsChild, std::move(copyGameObjects), std::move(cutGameObjects),
	                   std::move(pasteGameObjectsAsChild));
	if (!hierarchyChanged)
	{
		return hierarchyChanged;
	}
	bool changed = hierarchyChanged.value;
	ResourceId prefabOperation;
	bool applyPrefab = false;
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
				auto groupProperties = DrawGameObjectGroupProperties(
				    *objectGroup, primaryName ? primaryName.value : String{}, diagnostic, tagLayerSettings,
				    openTagLayerSettings,
				    overrideCommit ? overrideCommit.value : GroupCommitObserver{}, &scene, &selectedObjects,
				    true);
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
			if (scene.Contains(node))
			{
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
					for (Size componentIndex = 0; componentIndex < components.value.size(); ++componentIndex)
					{
						const ComponentId componentId = components.value[componentIndex];
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
						const String componentLabel = scriptHeader ? scriptHeader->TypeName()
						                              : descriptor ? descriptor->displayName
						                                           : codec.value->type;
						auto enabled = scene.IsComponentEnabled(componentId);
						const bool canEnable = enabled && object->SupportsEnabled();
						if (canEnable)
						{
							bool value = enabled.value;
							if (ImGui::Checkbox("##ComponentEnabled", &value))
							{
								auto set = scene.SetComponentEnabled(componentId, value);
								diagnostic = set ? String{} : set.error;
								changed |= static_cast<bool>(set);
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::SetTooltip("Enabled");
							}
							ImGui::SameLine();
						}
						const bool componentOpen = DrawInspectorComponentFoldout(
						    Icons::ForComponent(object->SerializationType(), scriptHeader != nullptr),
						    componentLabel.c_str());
						const ImVec2 headerMin = ImGui::GetItemRectMin();
						const ImVec2 headerMax = ImGui::GetItemRectMax();
						if (inspectorContext.selection)
						{
							EditorSelectionEntry owner;
							owner.kind = EditorSelectionKind::GameObject;
							owner.documentId = inspectorContext.documentId;
							owner.objectId = node->Id();
							const auto ownerEntry = std::find_if(
							    inspectorContext.selection->entries.begin(), inspectorContext.selection->entries.end(),
							    [&](const EditorSelectionEntry& entry)
							    {
								    return entry.kind == EditorSelectionKind::GameObject &&
								           entry.documentId == owner.documentId && entry.objectId == owner.objectId;
							    });
							if (ownerEntry != inspectorContext.selection->entries.end())
							{
								owner = *ownerEntry;
							}
							if (ImGui::BeginDragDropSource())
							{
								auto drag = BeginEditorComponentDragSession(
								    owner, id.value, object->SerializationType(), inspectorContext.selection->revision);
								if (drag)
								{
									ImGui::SetDragDropPayload(editorDragSessionPayloadType, &drag.value,
									                          sizeof(drag.value));
								}
								ImGui::TextUnformatted(componentLabel.c_str());
								ImGui::EndDragDropSource();
							}
						}
						ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 28.f);
						if (Icons::Button("ComponentActions", Icons::Icon::More, "Component actions"))
						{
							ImGui::OpenPopup("SceneComponentActions");
						}
						bool reorderedComponent = false;
						const ImRect headerRect(headerMin, headerMax);
						if (headerRect.Contains(ImGui::GetIO().MousePos) &&
						    ImGui::IsMouseClicked(ImGuiMouseButton_Right))
						{
							ImGui::OpenPopup("SceneComponentActions");
						}
						if (headerRect.GetHeight() > 0.f &&
						    ImGui::BeginDragDropTargetCustom(headerRect, ImGui::GetID("##ComponentOrderDropTarget")))
						{
							if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
							        editorDragSessionPayloadType, ImGuiDragDropFlags_AcceptNoDrawDefaultRect |
							                                          ImGuiDragDropFlags_AcceptBeforeDelivery))
							{
								auto decoded = DecodeEditorDragPayload(
								    payload->DataType, payload->Data, payload->DataSize,
								    inspectorContext.selection
								        ? std::optional<UInt64>(inspectorContext.selection->revision)
								        : std::nullopt);
								if (!decoded)
								{
									if (payload->IsDelivery())
									{
										diagnostic = decoded.error;
									}
								}
								else if (decoded.value.sources.size() == 1 &&
								         decoded.value.sources.front().identity.kind ==
								             EditorSelectionKind::GameObject &&
								         decoded.value.sources.front().identity.documentId ==
								             inspectorContext.documentId &&
								         decoded.value.sources.front().identity.objectId == node->Id() &&
								         decoded.value.sources.front().componentId)
								{
									const Float32 midpoint = (headerMin.y + headerMax.y) * 0.5f;
									const bool insertAfter = ImGui::GetIO().MousePos.y >= midpoint;
									const Float32 insertionY = insertAfter ? headerMax.y : headerMin.y;
									ImGui::GetWindowDrawList()->AddLine(
									    {headerMin.x, insertionY}, {headerMax.x, insertionY},
									    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.f);
									if (payload->IsDelivery() && ClaimEditorDragDropDelivery(payload))
									{
										const auto& source = decoded.value.sources.front();
										auto sourceId = scene.ComponentByPersistentId(source.componentId);
										const auto sourcePosition =
										    sourceId ? std::find(components.value.begin(), components.value.end(),
										                         sourceId.value)
										             : components.value.end();
										if (!sourceId || sourcePosition == components.value.end())
										{
											diagnostic = "Dragged component is no longer attached to this GameObject.";
										}
										else
										{
											Size targetIndex = componentIndex + (insertAfter ? 1 : 0);
											const Size sourceIndex =
											    static_cast<Size>(sourcePosition - components.value.begin());
											if (sourceIndex < targetIndex)
											{
												--targetIndex;
											}
											targetIndex = std::min(targetIndex, components.value.size() - 1);
											if (targetIndex != sourceIndex)
											{
												auto moved = scene.ReorderComponent(sourceId.value, targetIndex);
												diagnostic = moved ? String{} : moved.error;
												changed |= static_cast<bool>(moved);
												reorderedComponent = static_cast<bool>(moved);
											}
										}
									}
								}
								else if (payload->IsDelivery() && decoded.value.sources.size() != 1)
								{
									diagnostic = "Reorder one component at a time within the same GameObject.";
								}
							}
							ImGui::EndDragDropTarget();
						}
						if (reorderedComponent)
						{
							ImGui::PopID();
							break;
						}
						bool resetComponent = false;
						bool moveComponentUp = false;
						bool moveComponentDown = false;
						bool remove = false;
						bool copyComponent = false;
						bool pasteComponentValues = false;
						bool pasteComponentAsNew = false;
						const String componentType = scriptHeader
						                                ? "script:" + scriptHeader->TypeName()
						                                : "native:" + object->SerializationType();
						if (ImGui::BeginPopup("SceneComponentActions"))
						{
							const String factoryType = componentType.substr(componentType.find(':') + 1);
							ImGui::BeginDisabled(componentEditors.FindType(factoryType) == nullptr);
							resetComponent = Icons::MenuItem("Reset");
							ImGui::EndDisabled();
							ImGui::Separator();
							copyComponent = Icons::MenuItem("Copy Component");
							const bool canPasteComponent =
							    copiedComponentPayload && copiedComponentPayload->is_object() &&
							    copiedComponentPayload->contains("type") &&
							    ToUrsaString(copiedComponentPayload->at("type").get<std::string>()) == componentType &&
							    copiedComponentPayload->contains("fields") &&
							    copiedComponentPayload->at("fields").is_object();
							ImGui::BeginDisabled(!canPasteComponent);
							pasteComponentValues = Icons::MenuItem("Paste Component Values");
							ImGui::EndDisabled();
			const String copiedStableType = copiedComponentPayload && copiedComponentPayload->is_object() &&
			                                        copiedComponentPayload->contains("type") &&
			                                        copiedComponentPayload->at("type").is_string()
			                                    ? ToUrsaString(copiedComponentPayload->at("type").get<std::string>())
			                                    : String{};
			const String copiedFactoryType = copiedStableType.compare(0, 7, "native:") == 0 ||
			                                         copiedStableType.compare(0, 7, "script:") == 0
			                                     ? copiedStableType.substr(7)
			                                     : copiedStableType;
			const bool canPasteAsNew =
							    copiedComponentPayload && copiedComponentPayload->is_object() &&
							    copiedComponentPayload->contains("type") && copiedComponentPayload->contains("fields") &&
							    copiedComponentPayload->at("fields").is_object() &&
			    componentEditors.FindType(copiedFactoryType) &&
			    static_cast<bool>(componentEditors.CanCreate(copiedFactoryType, inspectorContext));
							ImGui::BeginDisabled(!canPasteAsNew);
							pasteComponentAsNew = Icons::MenuItem("Paste Component As New");
							ImGui::EndDisabled();
							ImGui::Separator();
							ImGui::BeginDisabled(componentIndex == 0);
							moveComponentUp = Icons::MenuItem("Move Up");
							ImGui::EndDisabled();
							ImGui::BeginDisabled(componentIndex + 1 >= components.value.size());
							moveComponentDown = Icons::MenuItem("Move Down");
							ImGui::EndDisabled();
							ImGui::Separator();
							remove = Icons::MenuItem("Remove Component");
							ImGui::EndPopup();
						}
						if (resetComponent)
						{
							const String factoryType = componentType.substr(componentType.find(':') + 1);
							auto reset = ResetComponentToDefaults(*object, factoryType, componentEditors, inspectorContext,
							                                    registry, loadContext, draft.get(), &scene);
							diagnostic = reset ? String{} : reset.error;
							changed |= reset && reset.value;
						}
						if (copyComponent)
						{
							ComponentSaveContext saveContext;
							saveContext.scene = &scene;
							saveContext.serialization = &loadContext;
							auto serialized = SerializedObject::Create(*object, registry, loadContext, saveContext);
							if (!serialized || !serialized.value->Snapshot().contains("Fields"))
							{
								diagnostic = serialized ? "Component has no serialized fields" : serialized.error;
							}
							else
							{
								const String copiedComponentType = scriptHeader
								                                ? "script:" + scriptHeader->TypeName()
								                                : "native:" + object->SerializationType();
								SerializedJson payload = SerializedJson::object();
								payload["type"] = ToStdString(copiedComponentType);
								payload["fields"] = serialized.value->Snapshot().at("Fields");
								copiedComponentPayload = std::move(payload);
							}
						}
						if (pasteComponentValues && copiedComponentPayload)
						{
							ComponentSaveContext saveContext;
							saveContext.scene = &scene;
							saveContext.serialization = &loadContext;
							auto serialized = SerializedObject::Create(*object, registry, loadContext, saveContext);
							if (!serialized)
							{
								diagnostic = serialized.error;
							}
							else
							{
								Vector<SerializedObject*> targets{serialized.value.get()};
								auto grouped = SerializedObjectGroup::Create(std::move(targets));
								if (!grouped)
								{
									diagnostic = grouped.error;
								}
								else
								{
									Vector<String> copiedFields;
									const auto current = grouped.value.TargetSnapshot(0);
									if (!current || !current.value.contains("Fields") ||
									    !current.value.at("Fields").is_object())
									{
										diagnostic = current ? "Component has no writable serialized fields" : current.error;
									}
									else
									{
										for (auto field = copiedComponentPayload->at("fields").begin();
										     field != copiedComponentPayload->at("fields").end(); ++field)
										{
											if (!current.value.at("Fields").contains(field.key()))
											{
												continue;
											}
											String escapedField = ToUrsaString(field.key());
											for (Size offset = 0; (offset = escapedField.find('~', offset)) != String::npos;
											     offset += 2)
											{
												escapedField.replace(offset, 1, "~0");
											}
											for (Size offset = 0; (offset = escapedField.find('/', offset)) != String::npos;
											     offset += 2)
											{
												escapedField.replace(offset, 1, "~1");
											}
											const String path = "/Fields/" + escapedField;
											auto set = grouped.value.SetValue(path, field.value());
											if (!set)
											{
												diagnostic = set.error;
												break;
											}
											copiedFields.push_back(path);
										}
									}
									if (diagnostic.empty() && !copiedFields.empty())
									{
										const auto changedTargets = grouped.value.PendingTargetChanges();
										auto applied = grouped.value.Apply();
										if (!applied)
										{
											diagnostic = applied.error;
										}
										else
										{
											Vector<Component*> componentTargets{object};
											auto commit = MakePrefabOverrideCommit(draft.get(), &scene, {}, &componentTargets);
											if (!commit)
											{
												diagnostic = commit.error;
											}
											else
											{
												for (const String& path : copiedFields)
												{
													auto committed = commit.value(grouped.value, path, changedTargets);
													if (!committed)
													{
														diagnostic = committed.error;
														break;
													}
												}
											}
										}
										changed |= std::any_of(changedTargets.begin(), changedTargets.end(),
										                      [](bool value) { return value; });
									}
								}
							}
						}
						if (pasteComponentAsNew && copiedComponentPayload)
						{
							const String copiedType =
							    ToUrsaString(copiedComponentPayload->at("type").get<std::string>());
							auto pasted = PasteComponentAsNew(componentEditors, copiedType, *copiedComponentPayload,
							                                 inspectorContext, registry, loadContext, &scene);
							diagnostic = pasted ? String{} : pasted.error;
							changed |= static_cast<bool>(pasted);
						}
						if (moveComponentUp || moveComponentDown)
						{
							const Size destination = moveComponentUp ? componentIndex - 1 : componentIndex + 1;
							auto moved = scene.ReorderComponent(componentId, destination);
							diagnostic = moved ? String{} : moved.error;
							changed |= static_cast<bool>(moved);
							ImGui::PopID();
							break;
						}
						if (remove)
						{
							auto removed = scene.RemoveComponent(componentId);
							diagnostic = removed ? String{} : removed.error;
							changed |= static_cast<bool>(removed);
							ImGui::PopID();
							continue;
						}
						if (componentOpen)
						{
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
								changed |= DrawSerializedScriptFields(*scriptData, workspace, inspectorContext,
								                                      diagnostic, scriptTypes);
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
								changed |= DrawCameraInspectorFields(*camera, diagnostic);
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
						}
						ImGui::PopID();
						ImGui::Spacing();
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
				const bool componentOpen = DrawInspectorComponentFoldout(
				    Icons::ForComponent(component->SerializationType(),
				                        dynamic_cast<Scripting::ScriptBehaviourComponent*>(component) != nullptr),
				    component->SerializationType().c_str());
				if (componentOpen)
				{
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
				}
				ImGui::PopID();
				ImGui::Spacing();
			}
			ImGui::TextDisabled("Runtime changes are discarded when Play stops.");
		}
	}
	ImGui::End();
}
} // namespace Ursa::Editor
