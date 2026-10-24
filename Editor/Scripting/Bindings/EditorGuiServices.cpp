#include <Scripting/Bindings/EditorServices.hpp>
#include <Assets/AssetPreviewExtension.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstring>
#include <limits>

namespace Ursa::Editor::Bindings
{
namespace
{
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
	if (changed && EditorServices::Scope(call).source.preview)
	{
		EditorServices::Scope(call).source.preview->RequestRepaint();
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

Result<void> EditorServices::SameLine(B::BindingCall& call)
{
	RequireGui(call);
	ImGui::SameLine();
	return {};
}

Result<void> EditorServices::Separator(B::BindingCall& call)
{
	RequireGui(call);
	ImGui::Separator();
	return {};
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

Result<bool> EditorServices::Toggle(B::BindingCall& call, const B::Utf8View& label, bool value)
{
	RequireGui(call);
	Edited(call, ImGui::Checkbox(GuiString(label).c_str(), &value));
	return Result<bool>::Success(value);
}

Result<Int64> EditorServices::IntField(B::BindingCall& call, const B::Utf8View& label, Int64 value)
{
	RequireGui(call);
	Edited(call, ImGui::InputScalar(GuiString(label).c_str(), ImGuiDataType_S64, &value));
	return Result<Int64>::Success(value);
}

Result<Float64> EditorServices::FloatField(B::BindingCall& call, const B::Utf8View& label, Float64 value)
{
	RequireGui(call);
	Finite(value);
	Edited(call, ImGui::InputDouble(GuiString(label).c_str(), &value));
	Finite(value);
	return Result<Float64>::Success(value);
}

Result<B::Utf8View> EditorServices::TextField(B::BindingCall& call, const B::Utf8View& label, const B::Utf8View& value)
{
	RequireGui(call);
	const String text = B::ReadString(value);
	Vector<char> buffer(text.size() + 4096, 0);
	std::memcpy(buffer.data(), text.data(), text.size());
	const bool edited = Edited(call, ImGui::InputText(GuiString(label).c_str(), buffer.data(), buffer.size()));
	return Result<B::Utf8View>::Success(call.arena.StringValue(edited ? String(buffer.data()) : text));
}

Result<Int32> EditorServices::Popup(B::BindingCall& call, const B::Utf8View& label, Int32 selected,
                                    const B::DataView& options)
{
	RequireGui(call);
	const auto names = B::ReadArray<B::Utf8View, String>(options, [](const auto& value) { return GuiString(value); });
	if (names.size() > static_cast<Size>(std::numeric_limits<Int32>::max()) || selected < -1 ||
	    (selected >= 0 && static_cast<Size>(selected) >= names.size()))
	{
		throw std::runtime_error("Popup selection is outside its options");
	}
	if (ImGui::BeginCombo(GuiString(label).c_str(), selected < 0 ? "" : names[static_cast<Size>(selected)].c_str()))
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
                                                                  const Vector2Value& value)
{
	RequireGui(call);
	Finite(value.F_X);
	Finite(value.F_Y);
	Float32 values[]{value.F_X, value.F_Y};
	Edited(call, ImGui::DragFloat2(GuiString(label).c_str(), values, 0.05f));
	return Result<Vector2Value>::Success({values[0], values[1]});
}

Result<EditorServices::Vector3Value> EditorServices::Vector3Field(B::BindingCall& call, const B::Utf8View& label,
                                                                  const Vector3Value& value)
{
	RequireGui(call);
	Finite(value.F_X);
	Finite(value.F_Y);
	Finite(value.F_Z);
	Float32 values[]{value.F_X, value.F_Y, value.F_Z};
	Edited(call, ImGui::DragFloat3(GuiString(label).c_str(), values, 0.05f));
	return Result<Vector3Value>::Success({values[0], values[1], values[2]});
}

Result<EditorServices::Vector4Value> EditorServices::Vector4Field(B::BindingCall& call, const B::Utf8View& label,
                                                                  const Vector4Value& value)
{
	RequireGui(call);
	Finite(value.F_X);
	Finite(value.F_Y);
	Finite(value.F_Z);
	Finite(value.F_W);
	Float32 values[]{value.F_X, value.F_Y, value.F_Z, value.F_W};
	Edited(call, ImGui::DragFloat4(GuiString(label).c_str(), values, 0.05f));
	return Result<Vector4Value>::Success({values[0], values[1], values[2], values[3]});
}

Result<EditorServices::QuaternionValue> EditorServices::QuaternionField(B::BindingCall& call, const B::Utf8View& label,
                                                                        const QuaternionValue& value)
{
	const auto result = B::TakeBindingResult(Vector4Field(call, label, {value.F_X, value.F_Y, value.F_Z, value.F_W}));
	return Result<QuaternionValue>::Success({result.F_X, result.F_Y, result.F_Z, result.F_W});
}

Result<EditorServices::ColorValue> EditorServices::ColorField(B::BindingCall& call, const B::Utf8View& label,
                                                              const ColorValue& value)
{
	RequireGui(call);
	Finite(value.F_R);
	Finite(value.F_G);
	Finite(value.F_B);
	Finite(value.F_A);
	Float32 values[]{value.F_R, value.F_G, value.F_B, value.F_A};
	Edited(call, ImGui::ColorEdit4(GuiString(label).c_str(), values));
	return Result<ColorValue>::Success({values[0], values[1], values[2], values[3]});
}

Result<EditorServices::OptionalReference> EditorServices::ObjectField(B::BindingCall& call, const B::Utf8View& label,
                                                                      const OptionalReference& value, UInt32 kind)
{
	RequireGui(call);
	auto& scope = Scope(call);
	if (kind != K::V_GameObject && kind != K::V_Component)
	{
		throw std::runtime_error("ObjectField requires a GameObject or Component kind");
	}
	OptionalReference result = value;
	EditorReferenceData current;
	if (value.present)
	{
		current = ReadReference(scope, value.F_value);
		if (current.kind != kind)
		{
			throw std::runtime_error("ObjectField reference kind does not match its control");
		}
	}
	if (ImGui::BeginCombo(GuiString(label).c_str(), value.present ? current.id.c_str() : "None"))
	{
		ComboScope comboScope;
		if (ImGui::Selectable("None", !result.present))
		{
			result = {};
		}
		Vector<GameObject::Ptr> objects = scope.source.selection.scene ? scope.source.selection.scene->RootGameObjects()
		                                                               : scope.source.selection.roots;
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
			const auto draw = [&](const String& id, const String& title)
			{
				ImGui::PushID(id.c_str());
				IdScope idScope;
				if (ImGui::Selectable(title.c_str(), value.present && current.id == id))
				{
					result = {1, WriteReference(call, Resolve(scope, kind, id))};
				}
			};
			if (kind == K::V_GameObject)
			{
				draw(object->Id().ToString(), name);
			}
			else
			{
				for (const auto* component : object->Components())
				{
					draw(component->PersistentId().ToString(), name + " / " + component->SerializationType());
				}
			}
		}
	}
	if (result.present)
	{
		result.F_value = WriteReference(call, ReadReference(scope, result.F_value));
	}
	return Result<OptionalReference>::Success(result);
}

Result<EditorServices::OptionalReference> EditorServices::AssetField(B::BindingCall& call, const B::Utf8View& label,
                                                                     const OptionalReference& value, UInt32 kind)
{
	RequireGui(call);
	auto& scope = Scope(call);
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
	String preview = !value.present ? String("None") : current.hasAsset ? current.asset.guid.ToString() : current.id;
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
			if (!expectedType.empty())
			{
				if (!scope.source.scriptTypes)
				{
					return false;
				}
				const auto candidate =
				    std::find_if(scope.source.scriptTypes->begin(), scope.source.scriptTypes->end(),
				                 [&](const auto& type) { return type.scriptType == asset.reference.typeId; });
				if (candidate == scope.source.scriptTypes->end() || candidate->kind != ScriptTypeKind::ScriptableObject)
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
		}
		return true;
	};
	if (ImGui::BeginCombo(GuiString(label).c_str(), preview.c_str()))
	{
		ComboScope comboScope;
		if (ImGui::Selectable("None", !result.present))
		{
			result = {};
		}
		for (const auto& asset : choices)
		{
			if (!compatible(asset))
			{
				continue;
			}
			const String key = asset.reference.asset.guid.ToString() + ":" +
			                   ToUrsaString(std::to_string(asset.reference.asset.localFileId));
			ImGui::PushID(key.c_str());
			IdScope idScope;
			if (ImGui::Selectable(asset.name.c_str(),
			                      value.present && current.asset.guid == asset.reference.asset.guid &&
			                          (current.asset.localFileId == asset.reference.asset.localFileId ||
			                           (!current.asset.localFileId && !asset.isSubAsset))))
			{
				result = {1, WriteReference(call, Resolve(scope, kind, {}, asset.reference.asset))};
			}
		}
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const auto* payload = ImGui::AcceptDragDropPayload("URSA_ASSET_REFERENCE"))
		{
			if (payload->DataSize == sizeof(AssetReference))
			{
				const auto reference = *static_cast<const AssetReference*>(payload->Data);
				for (const auto& asset : choices)
				{
					if (asset.reference.asset.guid == reference.guid && compatible(asset) &&
					    (asset.reference.asset.localFileId == reference.localFileId ||
					     (!reference.localFileId && !asset.isSubAsset)))
					{
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
