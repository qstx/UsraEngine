#include <Scripting/Bindings/EditorServices.hpp>
#include <Assets/AssetPreviewExtension.hpp>
#include <Assets/AssetPreviewContent.hpp>
#include <Inspector/ReferencePicker.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Scene/Scene.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

namespace Ursa::Editor::Bindings
{
namespace
{
struct PickerTypeScope
{
	String& target;
	String previous;

	PickerTypeScope(String& target, String value) : target(target), previous(std::move(target))
	{
		target = std::move(value);
	}

	~PickerTypeScope()
	{
		target = std::move(previous);
	}
};

using K = B::Wire::Enum_Ursa_Editor_EditorReferenceKind;

struct ComboScope
{
	~ComboScope()
	{
		ImGui::EndCombo();
	}
};

struct IdScope
{
	~IdScope()
	{
		ImGui::PopID();
	}
};

struct ArrayElementDragPayload
{
	ImGuiID arrayId;
	Int32 index;
};

struct MixedValueScope
{
	explicit MixedValueScope(bool mixed) : mixed(mixed)
	{
		if (mixed)
		{
			ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
		}
	}

	~MixedValueScope()
	{
		if (mixed)
		{
			ImGui::PopItemFlag();
		}
	}

	bool mixed;
};

struct PickerCandidate
{
	EditorReferenceData reference;
	String key;
	String name;
	String type;
	String source;
	std::function<Result<MaterialTexturePtr>()> loadTexturePreview;
	std::function<Result<std::shared_ptr<MaterialInstance>>()> loadMaterialPreview;
	std::function<Result<std::unique_ptr<AssetPreviewContent>>()> loadAssetPreviewContent;
	std::function<Result<std::shared_ptr<AssetPreviewExtension>>()> createCustomPreview;
};

Map<String, ResourceId> pendingComponentDropChoices;

String PickerTargetKey(const EditorInvocation& scope)
{
	String key = scope.source.targetIdentity;
	if (scope.source.assetSelection)
	{
		key += ":asset:" + scope.source.assetSelection->guid.ToString() + ":" +
		       ToUrsaString(std::to_string(scope.source.assetSelection->localFileId));
	}
	if (scope.source.target)
	{
		key += ":target:" + scope.source.target->PersistentId().ToString();
	}
	for (const Component* component : scope.source.componentTargets)
	{
		key += ":component:" + component->PersistentId().ToString();
	}
	if (scope.source.selection.object)
	{
		key += ":object:" + scope.source.selection.object->Id().ToString();
	}
	return key.empty() ? String("editor-context") : key;
}

String PickerReferenceType(UInt32 kind)
{
	switch (kind)
	{
		case K::V_GameObject:
			return "GameObject";
		case K::V_Component:
			return "Component";
		case K::V_Mesh:
			return "Mesh";
		case K::V_Material:
			return "Material";
		case K::V_Texture:
			return "Texture";
		case K::V_ScriptableObject:
			return "ScriptableObject";
		case K::V_Shader:
			return "Shader";
		case K::V_ComputeShader:
			return "ComputeShader";
		default:
			return "Asset";
	}
}

std::optional<Size> DrawReferencePicker(const String& controlScope, const String& label, const String& currentName,
                                        const String& expectedType, const Vector<PickerCandidate>& candidates,
                                        const String& currentKey)
{
	ImGui::BeginGroup();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label.c_str());
	ImGui::SameLine();
	Vector<ReferencePickerOption> options;
	options.reserve(candidates.size());
	for (const auto& candidate : candidates)
	{
		String source = candidate.source;
		if (candidate.reference.hasAsset)
		{
			source += " GUID=" + candidate.reference.asset.guid.ToString();
			if (candidate.reference.asset.localFileId)
			{
				source += " localID=" + ToUrsaString(std::to_string(candidate.reference.asset.localFileId));
			}
		}
		else if (!candidate.reference.id.empty())
		{
			source += " ID=" + candidate.reference.id;
		}
		ReferencePickerOption option{candidate.key,     candidate.name, candidate.type,
		                             std::move(source), true,           candidate.loadTexturePreview};
		option.loadMaterialPreview = candidate.loadMaterialPreview;
		option.loadAssetPreviewContent = candidate.loadAssetPreviewContent;
		option.createCustomPreview = candidate.createCustomPreview;
		options.push_back(std::move(option));
	}
	const auto selected = DrawReferencePickerButton(controlScope + ":" + label, currentName, expectedType, options,
	                                                currentKey, controlScope);
	ImGui::EndGroup();
	return selected;
}

String GuiString(const B::Utf8View& value)
{
	String text = B::ReadString(value);
	if (text.find('\0') != String::npos)
	{
		throw std::runtime_error("GUI strings cannot contain embedded NUL characters");
	}
	return text;
}

bool Edited(B::BindingCall& call, bool changed)
{
	auto& scope = EditorServices::Scope(call);
	scope.lastControlEdited = changed;
	if (changed && scope.source.preview)
	{
		scope.source.preview->RequestRepaint();
	}
	return changed;
}

void Finite(Float64 value)
{
	if (!std::isfinite(value))
	{
		throw std::runtime_error("Editor numeric controls require finite values");
	}
}
} // namespace

void EditorServices::RequireGui(B::BindingCall& call)
{
	const auto& scope = Scope(call);
	if (scope.source.preview && !scope.source.preview->GuiAllowed())
	{
		throw std::runtime_error(
		    "Preview layout controls require OnPreviewSettings; image drawing uses the supplied Rect");
	}
	if (!ImGui::GetCurrentContext() || !ImGui::GetCurrentContext()->WithinFrameScope)
	{
		throw std::runtime_error("GUI controls require an active Editor frame");
	}
}

Result<void> EditorServices::Text(B::BindingCall& call, const B::Utf8View& text)
{
	RequireGui(call);
	const String value = B::ReadString(text);
	ImGui::TextUnformatted(value.data(), value.data() + value.size());
	return {};
}

Result<bool> EditorServices::Button(B::BindingCall& call, const B::Utf8View& label)
{
	RequireGui(call);
	return Result<bool>::Success(Edited(call, ImGui::Button(GuiString(label).c_str())));
}

Result<bool> EditorServices::SmallButton(B::BindingCall& call, const B::Utf8View& label)
{
	RequireGui(call);
	return Result<bool>::Success(Edited(call, ImGui::SmallButton(GuiString(label).c_str())));
}

Result<bool> EditorServices::BeginPopupButton(B::BindingCall& call, const B::Utf8View& label,
                                              const B::Utf8View& popupId)
{
	RequireGui(call);
	const String id = GuiString(popupId);
	if (ImGui::SmallButton(GuiString(label).c_str()))
	{
		ImGui::OpenPopup(id.c_str());
	}
	return Result<bool>::Success(ImGui::BeginPopup(id.c_str()));
}

Result<void> EditorServices::EndPopup(B::BindingCall& call)
{
	RequireGui(call);
	ImGui::EndPopup();
	return {};
}

Result<bool> EditorServices::BeginMenu(B::BindingCall& call, const B::Utf8View& label)
{
	RequireGui(call);
	return Result<bool>::Success(ImGui::BeginMenu(GuiString(label).c_str()));
}

Result<void> EditorServices::EndMenu(B::BindingCall& call)
{
	RequireGui(call);
	ImGui::EndMenu();
	return {};
}

Result<bool> EditorServices::MenuItem(B::BindingCall& call, const B::Utf8View& label, bool enabled)
{
	RequireGui(call);
	return Result<bool>::Success(ImGui::MenuItem(GuiString(label).c_str(), nullptr, false, enabled));
}

Result<Int32> EditorServices::DragDropArrayElement(B::BindingCall& call, const B::Utf8View& id, Int32 index,
                                                   Int32 count)
{
	RequireGui(call);
	if (index < 0 || index >= count)
	{
		return Result<Int32>::Failure("Array element drag handle index is out of range");
	}
	const String nativeId = GuiString(id);
	const ImGuiID arrayId = ImGui::GetID(nativeId.c_str());
	const String label = String("::##") + nativeId + ".drag." + ToUrsaString(std::to_string(index));
	ImGui::SmallButton(label.c_str());
	if (ImGui::BeginDragDropSource())
	{
		const ArrayElementDragPayload payload{arrayId, index};
		ImGui::SetDragDropPayload("UrsaSerializedArrayElement", &payload, sizeof(payload));
		ImGui::Text("Element %d", index);
		ImGui::EndDragDropSource();
	}
	Int32 sourceIndex = -1;
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UrsaSerializedArrayElement"))
		{
			if (payload->DataSize == sizeof(ArrayElementDragPayload))
			{
				const auto& dragged = *static_cast<const ArrayElementDragPayload*>(payload->Data);
				if (dragged.arrayId == arrayId && dragged.index >= 0 && dragged.index < count && dragged.index != index)
				{
					sourceIndex = dragged.index;
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	return Result<Int32>::Success(sourceIndex);
}

Result<void> EditorServices::SameLine(B::BindingCall& call)
{
	RequireGui(call);
	ImGui::SameLine();
	return {};
}

Result<void> EditorServices::NewLine(B::BindingCall& call)
{
	RequireGui(call);
	ImGui::NewLine();
	return {};
}

Result<void> EditorServices::Separator(B::BindingCall& call)
{
	RequireGui(call);
	ImGui::Separator();
	return {};
}

Result<bool> EditorServices::FoldoutHeader(B::BindingCall& call, const B::Utf8View& label)
{
	RequireGui(call);
	return Result<bool>::Success(ImGui::TreeNodeEx(GuiString(label).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth));
}

Result<void> EditorServices::EndFoldout(B::BindingCall& call)
{
	RequireGui(call);
	ImGui::TreePop();
	return {};
}

Result<bool> EditorServices::IsLastItemEdited(B::BindingCall& call)
{
	RequireGui(call);
	return Result<bool>::Success(Scope(call).lastControlEdited);
}

Result<bool> EditorServices::IsLastItemActive(B::BindingCall& call)
{
	RequireGui(call);
	return Result<bool>::Success(ImGui::IsItemActive());
}

Result<void> EditorServices::BeginGroup(B::BindingCall& call)
{
	RequireGui(call);
	ImGui::BeginGroup();
	++Scope(call).groups;
	return {};
}

Result<void> EditorServices::EndGroup(B::BindingCall& call)
{
	RequireGui(call);
	auto& scope = Scope(call);
	if (scope.groups == 0)
	{
		throw std::runtime_error("Editor layout scope is unbalanced");
	}
	ImGui::EndGroup();
	--scope.groups;
	return {};
}

Result<bool> EditorServices::Toggle(B::BindingCall& call, const B::Utf8View& label, bool value, bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	Edited(call, ImGui::Checkbox(GuiString(label).c_str(), &value));
	return Result<bool>::Success(value);
}

Result<bool> EditorServices::ToggleLeft(B::BindingCall& call, const B::Utf8View& label, bool value, bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	const String id = "##ToggleLeft" + GuiString(label);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(GuiString(label).c_str());
	ImGui::SameLine();
	Edited(call, ImGui::Checkbox(id.c_str(), &value));
	return Result<bool>::Success(value);
}

Result<void> EditorServices::SetNextItemWidth(B::BindingCall& call, Float64 width)
{
	RequireGui(call);
	if (!std::isfinite(width) || std::abs(width) > std::numeric_limits<Float32>::max())
	{
		throw std::runtime_error("Editor control width must be finite and representable");
	}
	ImGui::SetNextItemWidth(static_cast<Float32>(width));
	return {};
}

Result<Int64> EditorServices::IntField(B::BindingCall& call, const B::Utf8View& label, Int64 value, bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	Edited(call, ImGui::InputScalar(GuiString(label).c_str(), ImGuiDataType_S64, &value));
	return Result<Int64>::Success(value);
}

Result<UInt64> EditorServices::ULongField(B::BindingCall& call, const B::Utf8View& label, UInt64 value, bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	Edited(call, ImGui::InputScalar(GuiString(label).c_str(), ImGuiDataType_U64, &value));
	return Result<UInt64>::Success(value);
}

Result<Float64> EditorServices::FloatField(B::BindingCall& call, const B::Utf8View& label, Float64 value,
                                           bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	Finite(value);
	Edited(call, ImGui::InputDouble(GuiString(label).c_str(), &value));
	Finite(value);
	return Result<Float64>::Success(value);
}

Result<Float64> EditorServices::SliderFloat(B::BindingCall& call, const B::Utf8View& label, Float64 value,
                                            Float64 minimum, Float64 maximum, bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	Finite(value);
	Finite(minimum);
	Finite(maximum);
	if (minimum > maximum || std::abs(value) > std::numeric_limits<Float32>::max() ||
	    std::abs(minimum) > std::numeric_limits<Float32>::max() ||
	    std::abs(maximum) > std::numeric_limits<Float32>::max())
	{
		throw std::runtime_error("SliderFloat requires finite, representable values and ordered bounds");
	}
	Float32 editedValue = static_cast<Float32>(value);
	const bool changed = Edited(call, ImGui::SliderFloat(GuiString(label).c_str(), &editedValue,
	                                                     static_cast<Float32>(minimum), static_cast<Float32>(maximum)));
	return Result<Float64>::Success(changed ? editedValue : value);
}

Result<Float64> EditorServices::DragFloat(B::BindingCall& call, const B::Utf8View& label, Float64 value, Float64 speed,
                                          Float64 minimum, Float64 maximum, const B::Utf8View& format, bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	Finite(value);
	Finite(speed);
	Finite(minimum);
	Finite(maximum);
	if (speed <= 0.0 || minimum > maximum || std::abs(value) > std::numeric_limits<Float32>::max() ||
	    speed > std::numeric_limits<Float32>::max() || std::abs(minimum) > std::numeric_limits<Float32>::max() ||
	    std::abs(maximum) > std::numeric_limits<Float32>::max())
	{
		throw std::runtime_error("DragFloat requires finite, representable values and ordered bounds");
	}
	Float32 editedValue = static_cast<Float32>(value);
	Edited(call,
	       ImGui::DragFloat(GuiString(label).c_str(), &editedValue, static_cast<Float32>(speed),
	                        static_cast<Float32>(minimum), static_cast<Float32>(maximum), GuiString(format).c_str()));
	return Result<Float64>::Success(editedValue);
}

Result<B::Utf8View> EditorServices::TextField(B::BindingCall& call, const B::Utf8View& label, const B::Utf8View& value,
                                              bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	const String text = B::ReadString(value);
	Vector<char> buffer(text.size() + 4096, 0);
	std::memcpy(buffer.data(), text.data(), text.size());
	const bool edited = Edited(call, ImGui::InputText(GuiString(label).c_str(), buffer.data(), buffer.size()));
	return Result<B::Utf8View>::Success(call.arena.StringValue(edited ? String(buffer.data()) : text));
}

Result<Int32> EditorServices::Popup(B::BindingCall& call, const B::Utf8View& label, Int32 selected,
                                    const B::DataView& options, bool mixedValue)
{
	RequireGui(call);
	const auto names = B::ReadArray<B::Utf8View, String>(options, [](const auto& value) { return GuiString(value); });
	if (names.size() > static_cast<Size>(std::numeric_limits<Int32>::max()) || selected < -1 ||
	    (selected >= 0 && static_cast<Size>(selected) >= names.size()))
	{
		throw std::runtime_error("Popup selection is outside its options");
	}
	const String preview = selected < 0 ? mixedValue ? "— Mixed —" : "" : names[static_cast<Size>(selected)];
	bool opened = false;
	{
		MixedValueScope mixed(mixedValue);
		opened = ImGui::BeginCombo(GuiString(label).c_str(), preview.c_str());
	}
	if (opened)
	{
		ComboScope comboScope;
		for (Size index = 0; index < names.size(); ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			IdScope idScope;
			if (ImGui::Selectable(names[index].c_str(), selected == static_cast<Int32>(index)))
			{
				selected = static_cast<Int32>(index);
				Edited(call, true);
			}
		}
	}
	return Result<Int32>::Success(selected);
}

Result<EditorServices::Vector2Value> EditorServices::Vector2Field(B::BindingCall& call, const B::Utf8View& label,
                                                                  const Vector2Value& value, bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	Finite(value.F_X);
	Finite(value.F_Y);
	Float32 values[]{value.F_X, value.F_Y};
	Edited(call, ImGui::DragFloat2(GuiString(label).c_str(), values, 0.05f));
	return Result<Vector2Value>::Success({values[0], values[1]});
}

Result<EditorServices::Vector3Value> EditorServices::Vector3Field(B::BindingCall& call, const B::Utf8View& label,
                                                                  const Vector3Value& value, bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	Finite(value.F_X);
	Finite(value.F_Y);
	Finite(value.F_Z);
	Float32 values[]{value.F_X, value.F_Y, value.F_Z};
	Edited(call, ImGui::DragFloat3(GuiString(label).c_str(), values, 0.05f));
	return Result<Vector3Value>::Success({values[0], values[1], values[2]});
}

Result<EditorServices::Vector4Value> EditorServices::Vector4Field(B::BindingCall& call, const B::Utf8View& label,
                                                                  const Vector4Value& value, bool mixedValue)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	Finite(value.F_X);
	Finite(value.F_Y);
	Finite(value.F_Z);
	Finite(value.F_W);
	Float32 values[]{value.F_X, value.F_Y, value.F_Z, value.F_W};
	Edited(call, ImGui::DragFloat4(GuiString(label).c_str(), values, 0.05f));
	return Result<Vector4Value>::Success({values[0], values[1], values[2], values[3]});
}

Result<EditorServices::QuaternionValue> EditorServices::QuaternionField(B::BindingCall& call, const B::Utf8View& label,
                                                                        const QuaternionValue& value, bool mixedValue)
{
	const auto result =
	    B::TakeBindingResult(Vector4Field(call, label, {value.F_X, value.F_Y, value.F_Z, value.F_W}, mixedValue));
	return Result<QuaternionValue>::Success({result.F_X, result.F_Y, result.F_Z, result.F_W});
}

Result<EditorServices::ColorValue> EditorServices::ColorField(B::BindingCall& call, const B::Utf8View& label,
                                                              const ColorValue& value, bool mixedValue, bool hdr)
{
	RequireGui(call);
	MixedValueScope mixed(mixedValue);
	Finite(value.F_R);
	Finite(value.F_G);
	Finite(value.F_B);
	Finite(value.F_A);
	Float32 values[]{value.F_R, value.F_G, value.F_B, value.F_A};
	const ImGuiColorEditFlags flags = hdr ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float : 0;
	Edited(call, ImGui::ColorEdit4(GuiString(label).c_str(), values, flags));
	return Result<ColorValue>::Success({values[0], values[1], values[2], values[3]});
}

Result<EditorServices::OptionalReference> EditorServices::ObjectField(B::BindingCall& call, const B::Utf8View& label,
                                                                      const OptionalReference& value, UInt32 kind,
                                                                      const B::Utf8View& expectedType,
                                                                      bool mixedValue)
{
	RequireGui(call);
	auto& scope = Scope(call);
	scope.lastControlEdited = false;
	PickerTypeScope expectedTypeScope(scope.expectedObjectType, B::ReadString(expectedType));
	if (kind != K::V_GameObject && kind != K::V_Component)
	{
		throw std::runtime_error("ObjectField requires a GameObject or Component kind");
	}
	OptionalReference result = value;
	const String controlKey = PickerTargetKey(scope) + ":" + GuiString(label);
	EditorReferenceData current;
	if (value.present)
	{
		current = ReadReference(scope, value.F_value);
		if (current.kind != kind)
		{
			throw std::runtime_error("ObjectField reference kind does not match its control");
		}
	}
	Vector<PickerCandidate> candidates;
	candidates.push_back({{kind, {}, {}, false, {}}, "none", "None", "None", {}});
	Vector<GameObject::Ptr> objects =
	    scope.source.selection.scene ? scope.source.selection.scene->RootGameObjects() : scope.source.selection.roots;
	if (objects.empty() && scope.source.selection.object)
	{
		auto root = scope.source.selection.object;
		while (root->Parent())
		{
			root = root->Parent();
		}
		objects.push_back(root);
	}
	while (!objects.empty())
	{
		auto object = std::move(objects.back());
		objects.pop_back();
		if (!object->IsValid())
		{
			continue;
		}
		for (const auto& child : object->Children())
		{
			objects.push_back(child);
		}
		const String name = B::TakeBindingResult(object->Name());
		ResourceId objectId = object->Id();
		if (scope.source.selection.scene)
		{
			auto persistent = scope.source.selection.scene->PersistentId(object);
			if (persistent)
			{
				objectId = persistent.value;
			}
		}
		if (kind == K::V_GameObject)
		{
			const String id = objectId.ToString();
			candidates.push_back({Resolve(scope, kind, id), "object:" + id, name, "GameObject", id});
		}
		else
		{
			for (Component* component :
			     CompatibleComponentsForReference(*object, scope.expectedObjectType, scope.source.scriptTypes))
			{
				ResourceId componentId = component->PersistentId();
				if (scope.source.selection.scene)
				{
					auto persistent = scope.source.selection.scene->ComponentPersistentId(component->Id());
					if (persistent)
					{
						componentId = persistent.value;
					}
				}
				const String id = componentId.ToString();
				const String type = component->SerializationType();
				candidates.push_back({Resolve(scope, kind, id), "component:" + id, name + " / " + type, type, name});
			}
		}
	}
	String currentName = mixedValue ? String("— Mixed —") : value.present ? "Missing " + current.id : "None";
	for (Size index = 1; !mixedValue && index < candidates.size(); ++index)
	{
		if (candidates[index].reference.id == current.id)
		{
			currentName = candidates[index].name;
			break;
		}
	}
	const auto chosen = DrawReferencePicker(
	    PickerTargetKey(scope), GuiString(label), currentName, kind == K::V_GameObject ? "GameObject" : "Component",
	    candidates, value.present ? String(kind == K::V_GameObject ? "object:" : "component:") + current.id : String{});
	if (chosen)
	{
		scope.lastControlEdited = true;
		result = *chosen == 0 ? OptionalReference{}
		                      : OptionalReference{1, WriteReference(call, candidates[*chosen].reference)};
	}
	if (kind == K::V_Component)
	{
		ImGui::PushID(controlKey.c_str());
		if (ImGui::BeginPopup("ChooseDroppedComponent"))
		{
			auto pending = pendingComponentDropChoices.find(controlKey);
			if (pending != pendingComponentDropChoices.end() && scope.source.selection.scene)
			{
				auto object = scope.source.selection.scene->GameObjectById(pending->second);
				if (object)
				{
					ImGui::TextUnformatted("Choose a compatible component");
					for (Component* component : CompatibleComponentsForReference(
					         *object.value, scope.expectedObjectType, scope.source.scriptTypes))
					{
						ResourceId componentId = component->PersistentId();
						auto persistent = scope.source.selection.scene->ComponentPersistentId(component->Id());
						if (persistent)
						{
							componentId = persistent.value;
						}
						const String item = component->SerializationType() + "##" + componentId.ToString();
						if (ImGui::Selectable(item.c_str()))
						{
							scope.lastControlEdited = true;
							result = {1, WriteReference(call, Resolve(scope, kind, componentId.ToString()))};
							pendingComponentDropChoices.erase(pending);
							ImGui::CloseCurrentPopup();
							break;
						}
					}
				}
				else
				{
					ImGui::TextDisabled("The dragged GameObject is no longer available.");
				}
			}
			if (ImGui::Button("Cancel"))
			{
				pendingComponentDropChoices.erase(controlKey);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const auto* payload = AcceptEditorDragDropPayload();
			    payload && scope.source.document && scope.source.selection.scene)
			{
				auto decoded = DecodeEditorDragPayload(payload->DataType, payload->Data, payload->DataSize);
				auto componentSource = decoded ? SingleComponentDragReference(decoded.value)
				                               : Result<ComponentDragReference>::Failure(decoded.error);
				if (componentSource)
				{
					const auto dragged = componentSource.value;
					if (dragged.owner.documentId != scope.source.document->Id())
					{
						ImGui::SetTooltip("A component reference must belong to the current editing document.");
					}
					else
					{
						auto componentId = scope.source.selection.scene->ComponentByPersistentId(dragged.componentId);
						Component* component =
						    componentId ? scope.source.selection.scene->GetComponent(componentId.value) : nullptr;
						auto owner = scope.source.selection.scene->GameObjectById(dragged.owner.objectId);
						Vector<Component*> compatible;
						if (owner)
						{
							compatible = CompatibleComponentsForReference(*owner.value, scope.expectedObjectType,
							                                              scope.source.scriptTypes);
						}
						if (component && owner && component->GetGameObject() == owner.value &&
						    std::find(compatible.begin(), compatible.end(), component) != compatible.end())
						{
							scope.lastControlEdited = true;
							result = {1, WriteReference(call, Resolve(scope, kind, dragged.componentId.ToString()))};
						}
						else
						{
							ImGui::SetTooltip("The dragged component is no longer available or is incompatible.");
						}
					}
				}
				else
				{
					auto draggedResult = decoded ? SingleGameObjectDragReference(decoded.value)
					                             : Result<GameObjectDragPayload>::Failure(decoded.error);
					if (draggedResult)
					{
						const auto dragged = draggedResult.value;
						if (dragged.documentId != scope.source.document->Id())
						{
							ImGui::SetTooltip("A GameObject reference must belong to the current editing document.");
						}
						auto object =
						    dragged.documentId == scope.source.document->Id()
						        ? scope.source.selection.scene->GameObjectById(dragged.objectId)
						        : Result<GameObject::Ptr>::Failure("The dragged object belongs to another document");
						Vector<Component*> compatible;
						if (object)
						{
							compatible = CompatibleComponentsForReference(*object.value, scope.expectedObjectType,
							                                              scope.source.scriptTypes);
						}
						if (compatible.size() == 1)
						{
							ResourceId componentId = compatible.front()->PersistentId();
							auto persistent =
							    scope.source.selection.scene->ComponentPersistentId(compatible.front()->Id());
							if (persistent)
							{
								componentId = persistent.value;
							}
							scope.lastControlEdited = true;
							result = {1, WriteReference(call, Resolve(scope, kind, componentId.ToString()))};
						}
						else if (compatible.size() > 1)
						{
							pendingComponentDropChoices[controlKey] = dragged.objectId;
							ImGui::OpenPopup("ChooseDroppedComponent");
						}
						else if (object)
						{
							ImGui::SetTooltip("The dragged GameObject has no compatible component.");
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopID();
	}
	if (result.present)
	{
		result.F_value = WriteReference(call, ReadReference(scope, result.F_value));
	}
	return Result<OptionalReference>::Success(result);
}

Result<EditorServices::OptionalReference> EditorServices::AssetField(B::BindingCall& call, const B::Utf8View& label,
                                                                     const OptionalReference& value, UInt32 kind,
                                                                     const B::Utf8View& expectedType,
                                                                     bool mixedValue)
{
	RequireGui(call);
	auto& scope = Scope(call);
	scope.lastControlEdited = false;
	PickerTypeScope expectedTypeScope(scope.expectedAssetType, B::ReadString(expectedType));
	if (kind < K::V_Asset || kind > K::V_ComputeShader)
	{
		throw std::runtime_error("AssetField requires an asset reference kind");
	}
	OptionalReference result = value;
	EditorReferenceData current;
	if (value.present)
	{
		current = ReadReference(scope, value.F_value);
		if ((kind != K::V_Asset && current.kind != kind) || current.kind < K::V_Asset)
		{
			throw std::runtime_error("AssetField reference kind does not match its control");
		}
	}
	String preview = !value.present     ? String("None")
	                 : current.hasAsset ? current.asset.guid.ToString()
	                 : (current.kind == K::V_Mesh || current.kind == K::V_Material)
	                     ? String("Local ") + PickerReferenceType(current.kind)
	                     : current.id;
	const auto choices = AssetChoices(scope);
	for (const auto& asset : choices)
	{
		if (value.present && current.asset.guid == asset.reference.asset.guid &&
		    (current.asset.localFileId == asset.reference.asset.localFileId ||
		     (!current.asset.localFileId && !asset.isSubAsset)))
		{
			preview = asset.name;
			break;
		}
	}
	const auto compatible = [&](const EditorAssetData& asset)
	{
		if (kind != K::V_Asset && asset.reference.kind != kind)
		{
			return false;
		}
		if (kind == K::V_ScriptableObject)
		{
			const String& expectedType = scope.expectedAssetType;
			if (!expectedType.empty() &&
			    (!scope.source.scriptTypes ||
			     !IsScriptReferenceTypeCompatible(*scope.source.scriptTypes, asset.reference.typeId, expectedType,
			                                      ScriptTypeKind::ScriptableObject)))
			{
				return false;
			}
		}
		return true;
	};
	Vector<PickerCandidate> candidates;
	candidates.push_back({{kind, {}, {}, false, {}}, "none", "None", "None", {}});
	for (const auto& asset : choices)
	{
		if (!compatible(asset))
		{
			continue;
		}
		const String key = "asset:" + asset.reference.asset.guid.ToString() + ":" +
		                   ToUrsaString(std::to_string(asset.reference.asset.localFileId));
		String type = asset.reference.typeId;
		if (type.empty())
		{
			type = PickerReferenceType(asset.reference.kind);
		}
		PickerCandidate candidate{asset.reference, key, asset.name, std::move(type), asset.path};
		if (asset.reference.kind == K::V_Texture)
		{
			const auto reference = asset.reference.asset;
			candidate.loadTexturePreview = [&scope, reference]
			{ return scope.Resources().LoadTexture(reference.guid, reference.localFileId); };
		}
		else if (asset.reference.kind == K::V_Material)
		{
			const auto reference = asset.reference.asset;
			candidate.loadMaterialPreview = [&scope, reference]
			{ return scope.Resources().LoadMaterial(reference.guid, reference.localFileId); };
		}
		if (scope.source.workspace && asset.reference.hasAsset)
		{
			AssetPreviewKind previewKind = AssetPreviewKind::None;
			switch (asset.reference.kind)
			{
				case K::V_Material:
					previewKind = AssetPreviewKind::Material;
					break;
				case K::V_Texture:
					previewKind = AssetPreviewKind::Texture;
					break;
				case K::V_Mesh:
					previewKind = AssetPreviewKind::Mesh;
					break;
				default:
					break;
			}
			auto parsedPath = VirtualPath::Parse(asset.path);
			const bool customScriptable =
			    asset.reference.kind == K::V_ScriptableObject && !asset.reference.typeId.empty();
			if ((previewKind != AssetPreviewKind::None || customScriptable) && parsedPath)
			{
				AssetPreviewTarget target;
				target.kind = previewKind;
				target.identity = asset.reference.asset;
				target.path = std::move(parsedPath.value);
				target.projectSession = ResourceId::Generate();
				target.custom = customScriptable;
				target.customTypeId = customScriptable ? asset.reference.typeId : String{};
				if (previewKind != AssetPreviewKind::None)
				{
					auto* workspace = scope.source.workspace;
					candidate.loadAssetPreviewContent = [workspace, target]
					{ return LoadAssetPreviewContent(target, *workspace); };
				}
				if (scope.source.createReferencePreview)
				{
					auto factory = scope.source.createReferencePreview;
					candidate.createCustomPreview = [factory = std::move(factory), target] { return factory(target); };
				}
			}
		}
		candidates.push_back(std::move(candidate));
	}
	const auto currentCandidate =
	    std::find_if(candidates.begin() + 1, candidates.end(),
	                 [&](const PickerCandidate& candidate)
	                 {
		                 return candidate.reference.asset.guid == current.asset.guid &&
		                        (candidate.reference.asset.localFileId == current.asset.localFileId ||
		                         (!current.asset.localFileId && !candidate.reference.asset.localFileId));
	                 });
	if (mixedValue)
	{
		preview = "— Mixed —";
	}
	else if (currentCandidate != candidates.end())
	{
		preview = currentCandidate->name;
	}
	else if (value.present)
	{
		preview = "Missing " + preview;
	}
	const String pickerType = scope.expectedAssetType.empty() ? PickerReferenceType(kind) : scope.expectedAssetType;
	const String currentKey = mixedValue || currentCandidate == candidates.end() ? String{} : currentCandidate->key;
	const auto chosen =
	    DrawReferencePicker(PickerTargetKey(scope), GuiString(label), preview, pickerType, candidates, currentKey);
	if (chosen)
	{
		scope.lastControlEdited = true;
		result = *chosen == 0
		             ? OptionalReference{}
		             : OptionalReference{
		                   1, WriteReference(call, Resolve(scope, kind, {}, candidates[*chosen].reference.asset))};
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const auto* payload = AcceptEditorDragDropPayload())
		{
			auto decoded = DecodeEditorDragPayload(payload->DataType, payload->Data, payload->DataSize);
			auto dropped =
			    decoded ? SingleAssetDragReference(decoded.value) : Result<AssetReference>::Failure(decoded.error);
			if (dropped)
			{
				const auto reference = dropped.value;
				for (const auto& asset : choices)
				{
					if (asset.reference.asset.guid == reference.guid && compatible(asset) &&
					    (asset.reference.asset.localFileId == reference.localFileId ||
					     (!reference.localFileId && !asset.isSubAsset)))
					{
						scope.lastControlEdited = true;
						result = {1, WriteReference(call, Resolve(scope, kind, {}, reference))};
						break;
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (result.present)
	{
		result.F_value = WriteReference(call, ReadReference(scope, result.F_value));
	}
	return Result<OptionalReference>::Success(result);
}
} // namespace Ursa::Editor::Bindings
