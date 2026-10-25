#include <Ui/EditorIcons.hpp>
#include <Inspector/AssetInspectorPanel.hpp>
#include <Inspector/ReferencePicker.hpp>
#include <Assets/ScriptableAssetInspector.hpp>
#include <Assets/ImportedAssetInspector.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Serialization/AssetReference.hpp>
#include <Serialization/MeshResource.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Workspace/ProjectWorkspace.hpp>
#include <Core/StringInterop.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

const char* AssetKindLabel(ResourceKind kind)
{
	switch (kind)
	{
		case ResourceKind::Texture:
			return "Texture";
		case ResourceKind::Material:
			return "Material";
		case ResourceKind::Scene:
			return "Scene";
		case ResourceKind::Prefab:
			return "Prefab";
		case ResourceKind::Mesh:
			return "Mesh";
		case ResourceKind::Shader:
			return "Shader";
	}
	return "Asset";
}

bool IsScriptableAsset(const VirtualPath& path)
{
	return PathFromUrsaString(path.relative).extension() == ".ursaasset";
}

const ScriptTypeDescriptor* FindScriptType(const Vector<ScriptTypeDescriptor>* types, const String& id)
{
	if (!types)
	{
		return nullptr;
	}
	const auto found = std::find_if(types->begin(), types->end(),
	                                [&](const ScriptTypeDescriptor& type) { return type.scriptType == id; });
	return found == types->end() ? nullptr : &*found;
}

String SelectionSignature(const EditorSelectionContext& selection)
{
	String signature = "primary:" + ToUrsaString(std::to_string(selection.primaryIndex));
	for (const auto& entry : selection.entries)
	{
		signature += "|";
		if (entry.kind == EditorSelectionKind::Asset)
		{
			signature += "asset:" + (entry.assetGuid ? entry.assetGuid.ToString() : entry.assetPath.ToString()) + ":" +
			             entry.assetPath.ToString() + ":" + ToUrsaString(std::to_string(entry.assetLocalFileId));
		}
		else
		{
			signature += "other:" + ToUrsaString(std::to_string(static_cast<int>(entry.kind)));
		}
	}
	return signature;
}

bool HasMaterialSourceSelection(const ProjectBrowserModel& model, const EditorSelectionContext& selection)
{
	return selection.entries.size() > 1 && std::all_of(selection.entries.begin(), selection.entries.end(),
	                                                   [&](const auto& entry)
	                                                   {
		                                                   const auto* asset = model.Find(entry.assetPath);
		                                                   return entry.kind == EditorSelectionKind::Asset &&
		                                                          !entry.IsMissing() && entry.assetLocalFileId == 0 &&
		                                                          asset && !asset->folder && asset->kind &&
		                                                          *asset->kind == ResourceKind::Material;
	                                                   });
}

bool HasScriptableSourceSelection(const ProjectBrowserModel& model, const EditorSelectionContext& selection)
{
	return selection.entries.size() > 1 && std::all_of(selection.entries.begin(), selection.entries.end(),
	                                                   [&](const auto& entry)
	                                                   {
		                                                   const auto* asset = model.Find(entry.assetPath);
		                                                   return entry.kind == EditorSelectionKind::Asset &&
		                                                          !entry.IsMissing() && entry.assetLocalFileId == 0 &&
		                                                          asset && !asset->folder &&
		                                                          IsScriptableAsset(asset->path);
	                                                   });
}

} // namespace

bool AssetInspectorPanel::HasModifiedSource() const
{
	return (nativeInspector && nativeInspector->Active() && nativeInspector->OwnsDocument() &&
	        nativeInspector->Document().Dirty()) ||
	       (scriptableInspector && scriptableInspector->Dirty()) ||
	       std::any_of(multiMaterialTargets.begin(), multiMaterialTargets.end(),
	                   [](const auto& target) { return target.inspector && target.inspector->Document().Dirty(); }) ||
	       std::any_of(multiScriptableTargets.begin(), multiScriptableTargets.end(),
	                   [](const auto& target) { return target.inspector && target.inspector->Dirty(); });
}

VirtualPath AssetInspectorPanel::ModifiedSourcePath() const
{
	if (nativeInspector && nativeInspector->Active() && nativeInspector->OwnsDocument() &&
	    nativeInspector->Document().Dirty())
	{
		return nativeInspector->Document().Path();
	}
	for (const auto& target : multiMaterialTargets)
	{
		if (target.inspector && target.inspector->Document().Dirty())
		{
			return target.source;
		}
	}
	for (const auto& target : multiScriptableTargets)
	{
		if (target.inspector && target.inspector->Dirty())
		{
			return target.source;
		}
	}
	return scriptableInspector && scriptableInspector->Dirty() ? scriptableInspector->SourcePath() : VirtualPath{};
}

bool AssetInspectorPanel::RetainsModifiedTargets(const EditorSelectionContext& next) const
{
	if (std::any_of(multiMaterialTargets.begin(), multiMaterialTargets.end(),
	                [](const auto& target) { return target.inspector && target.inspector->Document().Dirty(); }) ||
	    std::any_of(multiScriptableTargets.begin(), multiScriptableTargets.end(),
	                [](const auto& target) { return target.inspector && target.inspector->Dirty(); }))
	{
		return SelectionSignature(next) == multiMaterialSelectionKey;
	}
	const auto* primary = next.PrimaryEntry();
	return next.entries.size() == 1 && primary && primary->kind == EditorSelectionKind::Asset &&
	       primary->assetPath == ModifiedSourcePath();
}

Result<void> AssetInspectorPanel::SaveMultiMaterialTargets(const RequestImport& requestImport)
{
	for (const auto& target : multiMaterialTargets)
	{
		if (!target.inspector || !target.inspector->Document().Dirty())
		{
			continue;
		}
		const auto saved = target.inspector->Save();
		if (!saved)
		{
			return saved;
		}
		if (requestImport)
		{
			requestImport(target.source);
		}
	}
	return {};
}

String AssetInspectorPanel::MultiSelectionDiagnostic(const ProjectBrowserModel& model,
                                                     const EditorSelectionContext& selection) const
{
	if (HasMaterialSourceSelection(model, selection))
	{
		return {};
	}
	if (HasScriptableSourceSelection(model, selection))
	{
		return {};
	}
	if (std::any_of(selection.entries.begin(), selection.entries.end(),
	                [](const auto& entry) { return entry.assetLocalFileId != 0; }))
	{
		return "Imported sub-assets are read-only and cannot join a source asset edit group.";
	}
	const bool containsImporter = std::any_of(selection.entries.begin(), selection.entries.end(),
	                                          [&](const auto& entry)
	                                          {
		                                          const auto* asset = model.Find(entry.assetPath);
		                                          return asset && !asset->importer.empty();
	                                          });
	if (containsImporter)
	{
		return "Importer settings are edited per source; multi-selection does not apply importer changes.";
	}
	return "Batch asset editing requires compatible writable Material or ScriptableObject source assets.";
}

Result<bool> AssetInspectorPanel::DrawMultiMaterialInspector(
    ProjectWorkspace& workspace, const ProjectBrowserModel& model, const EditorSelectionContext& selection,
    const ComponentRegistry* components, EditorDocuments* existingDocuments, const RequestImport& requestImport)
{
	const String signature = SelectionSignature(selection);
	if (signature != multiMaterialSelectionKey)
	{
		multiMaterialTargets.clear();
		multiMaterialSelectionKey = signature;
		multiMaterialPrimaryIndex = selection.primaryIndex;
		multiMaterialDiagnostic.clear();
	}
	if (!HasMaterialSourceSelection(model, selection))
	{
		multiMaterialTargets.clear();
		return Result<bool>::Success(false);
	}
	if (existingDocuments && existingDocuments->Current() &&
	    std::any_of(selection.entries.begin(), selection.entries.end(),
	                [&](const auto& entry) { return existingDocuments->Current()->Path() == entry.assetPath; }))
	{
		multiMaterialTargets.clear();
		return Result<bool>::Failure("Close the active Material document before multi-editing its source");
	}
	if (multiMaterialTargets.empty())
	{
		for (const auto& entry : selection.entries)
		{
			const auto* asset = model.Find(entry.assetPath);
			if (!asset || !workspace.CheckWritable(asset->path))
			{
				multiMaterialTargets.clear();
				return Result<bool>::Failure("Every Material in a multi-edit group must be writable");
			}
			auto opened =
			    NativeAssetInspector::Open(workspace, asset->path, ResourceKind::Material, components, nullptr);
			if (!opened)
			{
				multiMaterialDiagnostic = opened.error;
				multiMaterialTargets.clear();
				return Result<bool>::Failure(opened.error);
			}
			multiMaterialTargets.push_back({asset->path, asset->guid, std::move(opened.value)});
		}
	}
	if (multiMaterialTargets.size() != selection.entries.size())
	{
		multiMaterialTargets.clear();
		return Result<bool>::Failure("Multi-material target group became incomplete");
	}
	Vector<std::unique_ptr<SerializedObject>> properties;
	Vector<SerializedObject*> targets;
	for (const auto& target : multiMaterialTargets)
	{
		auto property = SerializedObject::Create(target.inspector->Properties());
		if (!property)
		{
			multiMaterialDiagnostic = property.error;
			return Result<bool>::Failure(property.error);
		}
		targets.push_back(property.value.get());
		properties.push_back(std::move(property.value));
	}
	auto groupResult = SerializedObjectGroup::Create(std::move(targets), multiMaterialPrimaryIndex);
	if (!groupResult)
	{
		return Result<bool>::Failure(groupResult.error);
	}
	auto group = std::move(groupResult.value);
	const auto primaryDraft =
	    multiMaterialTargets[multiMaterialPrimaryIndex].inspector->Document().Payload<MaterialDraft>();
	const auto shader = primaryDraft ? primaryDraft->Material().Shader() : nullptr;
	if (!shader)
	{
		return Result<bool>::Failure("The selected Material has no Shader property schema");
	}
	const auto commonPaths = group.CommonPropertyPaths();
	bool changed = false;
	for (const auto& property : shader->properties)
	{
		const String path = "/Fields/" + property.identifier;
		if (std::find(commonPaths.begin(), commonPaths.end(), path) == commonPaths.end() ||
		    std::find(property.attributes.begin(), property.attributes.end(), "HideInInspector") !=
		        property.attributes.end())
		{
			continue;
		}
		auto description = group.DescribeProperty(path);
		if (!description)
		{
			continue;
		}
		const Json value = description.value.at("Value");
		const bool mixed = description.value.value("HasMultipleDifferentValues", false);
		const String label = property.displayName.empty() ? property.identifier : property.displayName;
		ImGui::PushID(property.identifier.c_str());
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
		bool edited = false;
		Json editedValue = value;
		switch (property.type)
		{
			case Shader::PropertyType::Float:
			case Shader::PropertyType::Range:
			{
				Float32 number = value.get<Float32>();
				edited = property.type == Shader::PropertyType::Range
				             ? ImGui::SliderFloat(label.c_str(), &number, property.rangeMinimum, property.rangeMaximum)
				             : ImGui::DragFloat(label.c_str(), &number, 0.01f);
				editedValue = number;
				break;
			}
			case Shader::PropertyType::Int:
			{
				Int32 number = value.get<Int32>();
				edited = property.identifier == "_AlphaMode"
				             ? ImGui::Combo(label.c_str(), &number, "Opaque\0Mask\0Blend\0")
				             : ImGui::InputInt(label.c_str(), &number);
				editedValue = number;
				break;
			}
			case Shader::PropertyType::Color:
			case Shader::PropertyType::Vector:
			{
				Float32 vector[4];
				for (Size index = 0; index < 4; ++index)
				{
					vector[index] = value.at(index).get<Float32>();
				}
				const bool hdr = std::find(property.attributes.begin(), property.attributes.end(), "HDR") !=
				                 property.attributes.end();
				edited = property.type == Shader::PropertyType::Color
				             ? ImGui::ColorEdit4(label.c_str(), vector,
				                                 hdr ? ImGuiColorEditFlags_HDR : ImGuiColorEditFlags_None)
				             : ImGui::DragFloat4(label.c_str(), vector, 0.01f);
				if (edited)
				{
					editedValue = {vector[0], vector[1], vector[2], vector[3]};
				}
				break;
			}
			case Shader::PropertyType::Texture2D:
			{
				const auto resources = workspace.Resources();
				String preview = mixed ? String("— Mixed —") : String("None");
				String currentKey;
				if (!mixed && !value.is_null())
				{
					auto reference =
					    AssetReferenceFromJson(value.at("$ursaReference").at("Resource"), "Material texture property");
					if (reference)
					{
						currentKey = "texture:" + reference.value.guid.ToString();
						const auto found = std::find_if(resources.begin(), resources.end(), [&](const auto& entry)
						                                { return entry.id == reference.value.guid; });
						if (found != resources.end())
						{
							preview = found->location.path.FileName();
						}
						else
						{
							preview = reference.value.guid.ToString();
						}
					}
				}
				Vector<ReferencePickerOption> options;
				options.push_back({"none", "None", "None", {}, true});
				Vector<ResourceId> textureIds;
				for (const auto& entry : resources)
				{
					if (entry.location.kind == ResourceKind::Texture)
					{
						textureIds.push_back(entry.id);
						ReferencePickerOption candidate{"texture:" + entry.id.ToString(),
						                                entry.location.path.FileName(), "Texture",
						                                entry.location.path.ToString(), true};
						const auto id = entry.id;
						candidate.loadTexturePreview = [&workspace, id]() -> Result<MaterialTexturePtr>
						{
							auto session = ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver());
							if (!session)
							{
								return Result<MaterialTexturePtr>::Failure(session.error);
							}
							return session.value->LoadTexture(id);
						};
						options.push_back(std::move(candidate));
					}
				}
				const auto selected = DrawReferencePickerButton(
				    "material-texture:" + ToUrsaString(std::to_string(ImGui::GetID(label.c_str()))), preview, "Texture",
				    options, currentKey);
				if (selected)
				{
					edited = true;
					if (*selected == 0)
					{
						editedValue = nullptr;
					}
					else
					{
						const auto id = textureIds[*selected - 1];
						editedValue = {
						    {"$ursaReference",
						     {{"Kind", "Texture"}, {"Id", ""}, {"Resource", AssetReferenceToJson({id, 0})}}}};
					}
				}
				break;
			}
		}
		ImGui::PopItemFlag();
		ImGui::PopID();
		if (edited)
		{
			auto set = group.SetValue(path, std::move(editedValue));
			if (!set)
			{
				multiMaterialDiagnostic = set.error;
				continue;
			}
			const auto changedTargets = group.PendingTargetChanges();
			auto applied = group.Apply();
			if (!applied)
			{
				multiMaterialDiagnostic = applied.error;
				continue;
			}
			for (Size index = 0; index < changedTargets.size(); ++index)
			{
				if (changedTargets[index])
				{
					multiMaterialTargets[index].inspector->Document().MarkDirty();
				}
			}
			changed = true;
			multiMaterialDiagnostic.clear();
		}
	}
	if (commonPaths.empty())
	{
		ImGui::TextDisabled("These Materials have no properties with matching Shader semantics.");
	}
	const auto dirtyCount =
	    static_cast<Size>(std::count_if(multiMaterialTargets.begin(), multiMaterialTargets.end(),
	                                    [](const auto& target) { return target.inspector->Document().Dirty(); }));
	if (dirtyCount)
	{
		ImGui::TextDisabled("%llu Material drafts have unsaved changes", static_cast<UInt64>(dirtyCount));
		ImGui::SameLine();
		if (Icons::Button("Save Selected Materials"))
		{
			auto saved = SaveMultiMaterialTargets(requestImport);
			multiMaterialDiagnostic = saved ? String{} : saved.error;
		}
	}
	if (!multiMaterialDiagnostic.empty())
	{
		ImGui::TextWrapped("%s", multiMaterialDiagnostic.c_str());
	}
	return Result<bool>::Success(changed);
}

Result<void> AssetInspectorPanel::SaveMultiScriptableTargets(const RequestImport& requestImport)
{
	for (const auto& target : multiScriptableTargets)
	{
		if (!target.inspector || !target.inspector->Dirty())
		{
			continue;
		}
		auto saved = target.inspector->Save();
		if (!saved)
		{
			return saved;
		}
		if (requestImport)
		{
			requestImport(target.source);
		}
	}
	return {};
}

Result<bool> AssetInspectorPanel::DrawMultiScriptableInspector(ProjectWorkspace& workspace,
                                                               const ProjectBrowserModel& model,
                                                               const EditorSelectionContext& selection,
                                                               const Vector<ScriptTypeDescriptor>* scriptTypes,
                                                               const RequestImport& requestImport)
{
	const String signature = SelectionSignature(selection);
	if (signature != multiMaterialSelectionKey)
	{
		multiMaterialTargets.clear();
		multiScriptableTargets.clear();
		multiMaterialSelectionKey = signature;
		multiMaterialPrimaryIndex = selection.primaryIndex;
		multiMaterialDiagnostic.clear();
	}
	if (!HasScriptableSourceSelection(model, selection))
	{
		return Result<bool>::Success(false);
	}
	if (multiScriptableTargets.empty())
	{
		String commonType;
		for (const auto& entry : selection.entries)
		{
			const auto* assetEntry = model.Find(entry.assetPath);
			if (!assetEntry || !workspace.CheckWritable(assetEntry->path))
			{
				return Result<bool>::Failure("Every ScriptableObject in a multi-edit group must be writable");
			}
			auto source = LoadScriptableAssetSource(workspace.Index().Filesystem(), assetEntry->path, assetEntry->guid);
			if (!source)
			{
				return Result<bool>::Failure(source.error);
			}
			const auto* type = FindScriptType(scriptTypes, source.value.TypeId());
			if (!type)
			{
				return Result<bool>::Failure("A selected ScriptableObject type is unavailable");
			}
			if (commonType.empty())
			{
				commonType = type->scriptType;
			}
			else if (commonType != type->scriptType)
			{
				return Result<bool>::Failure("ScriptableObject multi-edit requires the same exact script type");
			}
			auto opened = ScriptableAssetInspector::Open(workspace, assetEntry->path, *type);
			if (!opened)
			{
				multiScriptableTargets.clear();
				return Result<bool>::Failure(opened.error);
			}
			multiScriptableTargets.push_back({assetEntry->path, assetEntry->guid, std::move(opened.value)});
		}
	}
	if (multiScriptableTargets.size() != selection.entries.size())
	{
		multiScriptableTargets.clear();
		return Result<bool>::Failure("Multi-ScriptableObject target group became incomplete");
	}
	Vector<std::unique_ptr<SerializedObject>> properties;
	Vector<SerializedObject*> targets;
	for (const auto& target : multiScriptableTargets)
	{
		auto property = SerializedObject::Create(target.inspector->Properties());
		if (!property)
		{
			return Result<bool>::Failure(property.error);
		}
		targets.push_back(property.value.get());
		properties.push_back(std::move(property.value));
	}
	auto groupResult = SerializedObjectGroup::Create(std::move(targets), multiMaterialPrimaryIndex);
	if (!groupResult)
	{
		return Result<bool>::Failure(groupResult.error);
	}
	auto group = std::move(groupResult.value);
	const auto commonPaths = group.CommonPropertyPaths();
	bool changed = false;
	for (const auto& path : commonPaths)
	{
		if (path.rfind("/Fields/", 0) != 0 || path.find('/', 8) != String::npos)
		{
			continue;
		}
		auto description = group.DescribeProperty(path);
		if (!description)
		{
			continue;
		}
		const String field = path.substr(String("/Fields/").size());
		const String label = ToUrsaString(description.value.value("DisplayName", std::string(field.c_str())));
		const String kind = ToUrsaString(description.value.value("Kind", std::string{}));
		Json editedValue = description.value.at("Value");
		bool edited = false;
		ImGui::PushID(path.c_str());
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, description.value.value("HasMultipleDifferentValues", false));
		if (kind == "Boolean")
		{
			bool value = editedValue.get<bool>();
			edited = ImGui::Checkbox(label.c_str(), &value);
			editedValue = value;
		}
		else if (kind == "FloatingPoint")
		{
			Float32 value = editedValue.get<Float32>();
			edited = ImGui::DragFloat(label.c_str(), &value, 0.01f);
			editedValue = value;
		}
		else if (kind == "SignedInteger" || kind == "Enumeration")
		{
			Int32 value = editedValue.get<Int32>();
			edited = ImGui::InputInt(label.c_str(), &value);
			editedValue = value;
		}
		else if (kind == "String")
		{
			char value[512]{};
			const String current = ToUrsaString(editedValue.get<std::string>());
			std::memcpy(value, current.c_str(), std::min(current.size(), sizeof(value) - 1));
			edited = ImGui::InputText(label.c_str(), value, sizeof(value));
			editedValue = value;
		}
		else if (kind == "Color" || kind == "Vector2" || kind == "Vector3" || kind == "Vector4")
		{
			const Size count = kind == "Vector2" ? 2 : kind == "Vector3" ? 3 : 4;
			Float32 value[4]{};
			for (Size index = 0; index < count; ++index)
			{
				value[index] = editedValue.at(index).get<Float32>();
			}
			edited = kind == "Color" ? ImGui::ColorEdit4(label.c_str(), value)
			         : count == 2    ? ImGui::DragFloat2(label.c_str(), value, 0.01f)
			         : count == 3    ? ImGui::DragFloat3(label.c_str(), value, 0.01f)
			                         : ImGui::DragFloat4(label.c_str(), value, 0.01f);
			if (edited)
			{
				editedValue = Json::array();
				for (Size index = 0; index < count; ++index)
				{
					editedValue.push_back(value[index]);
				}
			}
		}
		else
		{
			ImGui::Text("%s", label.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("No multi-object drawer for %s", kind.c_str());
		}
		ImGui::PopItemFlag();
		ImGui::PopID();
		if (edited)
		{
			auto set = group.SetValue(path, std::move(editedValue));
			if (!set)
			{
				multiMaterialDiagnostic = set.error;
				continue;
			}
			auto applied = group.Apply();
			if (!applied)
			{
				multiMaterialDiagnostic = applied.error;
				continue;
			}
			changed = true;
			multiMaterialDiagnostic.clear();
		}
	}
	if (commonPaths.empty())
	{
		ImGui::TextDisabled("These ScriptableObjects have no common editable fields.");
	}
	const auto dirtyCount =
	    static_cast<Size>(std::count_if(multiScriptableTargets.begin(), multiScriptableTargets.end(),
	                                    [](const auto& target) { return target.inspector->Dirty(); }));
	if (dirtyCount)
	{
		ImGui::TextDisabled("%llu ScriptableObject drafts have unsaved changes", static_cast<UInt64>(dirtyCount));
		ImGui::SameLine();
		if (Icons::Button("Save Selected ScriptableObjects"))
		{
			auto saved = SaveMultiScriptableTargets(requestImport);
			multiMaterialDiagnostic = saved ? String{} : saved.error;
		}
	}
	if (!multiMaterialDiagnostic.empty())
	{
		ImGui::TextWrapped("%s", multiMaterialDiagnostic.c_str());
	}
	return Result<bool>::Success(changed);
}

ImporterSelectionResult AssetInspectorPanel::DrawSourceSelectionPrompt(const RequestImport& requestImport, bool canSave)
{
	if (!ImGui::BeginPopupModal("Unsaved Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return ImporterSelectionResult::Pending;
	}
	const auto path = ModifiedSourcePath();
	if (path.scheme.empty())
	{
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return ImporterSelectionResult::Accepted;
	}
	const bool multiMaterials =
	    !multiMaterialTargets.empty() &&
	    std::any_of(multiMaterialTargets.begin(), multiMaterialTargets.end(),
	                [](const auto& target) { return target.inspector && target.inspector->Document().Dirty(); });
	const bool multiScriptables =
	    std::any_of(multiScriptableTargets.begin(), multiScriptableTargets.end(),
	                [](const auto& target) { return target.inspector && target.inspector->Dirty(); });
	if (multiMaterials || multiScriptables)
	{
		const auto count = static_cast<Size>(
		    std::count_if(multiMaterialTargets.begin(), multiMaterialTargets.end(),
		                  [](const auto& target) { return target.inspector && target.inspector->Document().Dirty(); }) +
		    std::count_if(multiScriptableTargets.begin(), multiScriptableTargets.end(),
		                  [](const auto& target) { return target.inspector && target.inspector->Dirty(); }));
		ImGui::Text("Save changes to %llu selected asset drafts before leaving?", static_cast<UInt64>(count));
	}
	else
	{
		ImGui::Text("Save changes to %s before leaving this asset?", path.ToString().c_str());
	}
	auto result = ImporterSelectionResult::Pending;
	ImGui::BeginDisabled(!canSave);
	if (Icons::Button("Save"))
	{
		const auto saved = multiMaterials     ? SaveMultiMaterialTargets(requestImport)
		                   : multiScriptables ? SaveMultiScriptableTargets(requestImport)
		                   : nativeInspector && nativeInspector->Active() && nativeInspector->OwnsDocument() &&
		                           nativeInspector->Document().Dirty()
		                       ? nativeInspector->Save()
		                   : scriptableInspector ? scriptableInspector->Save()
		                                         : Result<void>::Failure("No source asset draft is active");
		if (saved)
		{
			if (!(multiMaterials || multiScriptables) && requestImport)
			{
				requestImport(path);
			}
			result = ImporterSelectionResult::Accepted;
		}
		else
		{
			nativeDiagnostic = saved.error;
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (Icons::Button("Discard"))
	{
		Reset();
		result = ImporterSelectionResult::Accepted;
	}
	ImGui::SameLine();
	if (Icons::Button("Cancel"))
	{
		result = ImporterSelectionResult::Cancelled;
	}
	if (!nativeDiagnostic.empty())
	{
		ImGui::TextWrapped("%s", nativeDiagnostic.c_str());
	}
	if (result != ImporterSelectionResult::Pending)
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
	return result;
}

Result<bool> AssetInspectorPanel::SaveSelectedSource(const EditorSelectionContext& selection)
{
	if (!multiScriptableTargets.empty() && SelectionSignature(selection) == multiMaterialSelectionKey)
	{
		const bool dirty =
		    std::any_of(multiScriptableTargets.begin(), multiScriptableTargets.end(),
		                [](const auto& target) { return target.inspector && target.inspector->Dirty(); });
		if (dirty)
		{
			auto saved = SaveMultiScriptableTargets({});
			return saved ? Result<bool>::Success(true) : Result<bool>::Failure(saved.error);
		}
	}
	if (!multiMaterialTargets.empty() && SelectionSignature(selection) == multiMaterialSelectionKey)
	{
		const bool dirty = std::any_of(multiMaterialTargets.begin(), multiMaterialTargets.end(), [](const auto& target)
		                               { return target.inspector && target.inspector->Document().Dirty(); });
		if (dirty)
		{
			auto saved = SaveMultiMaterialTargets({});
			return saved ? Result<bool>::Success(true) : Result<bool>::Failure(saved.error);
		}
	}
	if (selection.kind != EditorSelectionKind::Asset || selection.assetMissing || selection.assetLocalFileId)
	{
		return Result<bool>::Success(false);
	}
	if (nativeInspector && nativeInspector->Active() && nativeInspector->Document().Path() == selection.assetPath &&
	    nativeInspector->Document().Dirty())
	{
		const auto saved = nativeInspector->Save();
		return saved ? Result<bool>::Success(true) : Result<bool>::Failure(saved.error);
	}
	if (scriptableInspector && scriptableInspector->SourcePath() == selection.assetPath && scriptableInspector->Dirty())
	{
		const auto saved = scriptableInspector->Save();
		return saved ? Result<bool>::Success(true) : Result<bool>::Failure(saved.error);
	}
	return Result<bool>::Success(false);
}

EditorDocument* AssetInspectorPanel::SelectedDocument(const EditorSelectionContext& selection) const
{
	return selection.kind == EditorSelectionKind::Asset && !selection.assetMissing && !selection.assetLocalFileId &&
	               nativeInspector && nativeInspector->Active() &&
	               nativeInspector->Document().Path() == selection.assetPath
	           ? &nativeInspector->Document()
	           : nullptr;
}

void AssetInspectorPanel::Draw(
    ProjectWorkspace& workspace, const ProjectBrowserModel& model, const EditorSelectionContext& selection, bool& open,
    const Vector<ScriptTypeDescriptor>* scriptTypes, const OpenAsset& openAsset, const RequestImport& requestImport,
    Float32 footerHeight, const std::function<void(Float32)>& footer, const std::function<void()>& properties,
    const std::function<Result<bool>(ManagedEditorContext&)>& custom, const ComponentRegistry* components,
    const NativeAssetInspector::DefaultDraw& nativeDefault, EditorDocuments* existingDocuments)
{
	if (!open || selection.kind != EditorSelectionKind::Asset)
	{
		return;
	}
	const auto flags =
	    footer ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None;
	const bool visible = ImGui::Begin("Inspector", &open, flags);
	Icons::DrawPanelContextMenu("AssetInspectorPanelActions", &open);
	if (!visible)
	{
		ImGui::End();
		return;
	}
	Float32 maximumFooterHeight = 0;
	if (footer)
	{
		const auto availableHeight = ImGui::GetContentRegionAvail().y;
		const auto spacing = ImGui::GetStyle().ItemSpacing.y;
		maximumFooterHeight = std::max(0.f, availableHeight - 40.f - spacing);
		const auto effectiveFooterHeight = std::clamp(footerHeight, 0.f, maximumFooterHeight);
		ImGui::BeginChild("AssetProperties",
		                  ImVec2(0, std::max(40.f, availableHeight - effectiveFooterHeight - spacing)),
		                  ImGuiChildFlags_None);
	}
	if (selection.entries.size() > 1)
	{
		ImGui::Text("%llu assets selected", static_cast<UInt64>(selection.entries.size()));
		ImGui::TextDisabled("Primary: %s", selection.assetPath.ToString().c_str());
		ImGui::Separator();
		const String restriction = MultiSelectionDiagnostic(model, selection);
		if (!restriction.empty())
		{
			multiMaterialTargets.clear();
			multiScriptableTargets.clear();
			multiMaterialSelectionKey = SelectionSignature(selection);
			multiMaterialDiagnostic = restriction;
			ImGui::TextWrapped("%s", restriction.c_str());
		}
		else
		{
			auto drawn = HasMaterialSourceSelection(model, selection)
			                 ? DrawMultiMaterialInspector(workspace, model, selection, components, existingDocuments,
			                                              requestImport)
			                 : DrawMultiScriptableInspector(workspace, model, selection, scriptTypes, requestImport);
			if (!drawn)
			{
				multiMaterialDiagnostic = drawn.error;
				ImGui::TextWrapped("%s", multiMaterialDiagnostic.c_str());
			}
		}
		if (footer)
		{
			ImGui::EndChild();
			footer(maximumFooterHeight);
		}
		ImGui::End();
		return;
	}
	const ProjectBrowserEntry* asset = selection.assetMissing ? nullptr : model.Find(selection.assetPath);
	if (asset && selection.assetGuid && asset->guid != selection.assetGuid)
	{
		asset = nullptr;
	}
	if (!asset)
	{
		ImGui::TextDisabled("The selected asset is no longer available.");
		ImGui::Text("GUID: %s | Local ID: %llu", selection.assetGuid.ToString().c_str(), selection.assetLocalFileId);
	}
	else
	{
		Icons::Text(Icons::ForAsset(asset->folder, asset->kind, asset->importer, asset->path.relative),
		            asset->name.c_str());
		ImGui::Separator();
		ImGui::Text("Path: %s", asset->path.ToString().c_str());
		ImGui::Text("GUID: %s", asset->guid.ToString().c_str());
		if (selection.assetLocalFileId)
		{
			const auto artifact =
			    std::find_if(asset->importedObjects.begin(), asset->importedObjects.end(),
			                 [&](const auto& object) { return object.localFileId == selection.assetLocalFileId; });
			if (artifact == asset->importedObjects.end())
			{
				ImGui::TextDisabled("The selected imported object is no longer available.");
			}
			else
			{
				const String key = PathToUrsaString(workspace.ProjectRoot()) + ":" + asset->guid.ToString() + ":" +
				                   ToUrsaString(std::to_string(artifact->localFileId)) + ":" +
				                   ToUrsaString(std::to_string(workspace.Database()->Revision()));
				if (key != inspectedImportedKey)
				{
					inspectedImportedKey = key;
					importedInspector.reset();
					const auto opened =
					    ImportedAssetInspector::Open(workspace, {asset->guid, artifact->localFileId}, artifact->kind);
					if (opened)
					{
						importedInspector = opened.value;
						importedDiagnostic.clear();
					}
					else
					{
						importedDiagnostic = opened.error;
					}
				}
				ImGui::Text("Object: %s", artifact->Label().c_str());
				if (importedInspector)
				{
					const auto drawn = importedInspector->Draw(custom);
					if (!drawn)
					{
						importedDiagnostic = drawn.error;
					}
				}
				if (!importedDiagnostic.empty())
				{
					ImGui::TextWrapped("%s", importedDiagnostic.c_str());
				}
			}
		}
		else if (asset->folder)
		{
			ImGui::TextUnformatted("Type: Folder");
		}
		else if (IsScriptableAsset(asset->path))
		{
			ImGui::TextUnformatted("Type: ScriptableObject");
			const String key =
			    PathToUrsaString(workspace.ProjectRoot()) + ":" + asset->path.ToString() + ":" + asset->guid.ToString();
			if (key != inspectedScriptableKey || !scriptableInspector)
			{
				inspectedScriptableKey = key;
				scriptableInspector.reset();
				scriptableDiagnostic.clear();
				auto loaded = LoadScriptableAssetSource(workspace.Index().Filesystem(), asset->path, asset->guid);
				const auto* type = loaded ? FindScriptType(scriptTypes, loaded.value.TypeId()) : nullptr;
				if (loaded && type)
				{
					auto opened = ScriptableAssetInspector::Open(workspace, asset->path, *type);
					if (opened)
					{
						scriptableInspector = std::move(opened.value);
					}
					else
					{
						scriptableDiagnostic = opened.error;
					}
				}
				else
				{
					scriptableDiagnostic =
					    loaded ? "Missing ScriptableObject type; original serialized fields are preserved"
					           : loaded.error;
				}
			}
			if (scriptableInspector)
			{
				ImGui::Text("Script Type: %s", scriptableInspector->Asset().TypeId().c_str());
				scriptableInspector->Draw(requestImport, custom, scriptTypes);
			}
			if (!scriptableDiagnostic.empty())
			{
				ImGui::TextWrapped("%s", scriptableDiagnostic.c_str());
			}
		}
		else if (asset->kind)
		{
			ImGui::Text("Type: %s", AssetKindLabel(*asset->kind));
			if (*asset->kind == ResourceKind::Material || *asset->kind == ResourceKind::Prefab)
			{
				String key = PathToUrsaString(workspace.ProjectRoot()) + ":" + asset->path.ToString() + ":" +
				             asset->guid.ToString();
				if (existingDocuments && existingDocuments->Current() &&
				    existingDocuments->Current()->Path() == asset->path)
				{
					key += ":document:" +
					       ToUrsaString(std::to_string(reinterpret_cast<UIntPtr>(existingDocuments->Current()))) + ":" +
					       ToUrsaString(std::to_string(existingDocuments->Current()->SaveVersion()));
				}
				if (key != inspectedNativeKey)
				{
					inspectedNativeKey = key;
					nativeInspector.reset();
					const auto opened =
					    NativeAssetInspector::Open(workspace, asset->path, *asset->kind, components, existingDocuments);
					if (opened)
					{
						nativeInspector = opened.value;
						nativeDiagnostic.clear();
					}
					else
					{
						nativeDiagnostic = opened.error;
					}
				}
				if (nativeInspector)
				{
					const auto drawn = nativeInspector->Draw(requestImport, custom, nativeDefault);
					if (!drawn && nativeInspector->OwnsDocument() && !nativeInspector->Document().Dirty())
					{
						inspectedNativeKey.clear();
						nativeInspector.reset();
					}
				}
				if (!nativeDiagnostic.empty())
				{
					ImGui::TextWrapped("%s", nativeDiagnostic.c_str());
				}
			}
			if (*asset->kind == ResourceKind::Mesh)
			{
				const auto revision = workspace.Database() ? workspace.Database()->Revision() : 0;
				const String meshKey = PathToUrsaString(workspace.ProjectRoot()) + ":" + asset->path.ToString() + ":" +
				                       asset->guid.ToString() + ":" + ToUrsaString(std::to_string(revision));
				if (meshKey != inspectedMeshKey)
				{
					inspectedMeshKey = meshKey;
					inspectedMesh.reset();
					auto session = ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver());
					if (session)
					{
						SerializationContext context(session.value.get());
						auto loaded = LoadMeshResource(workspace.Index().Filesystem(), asset->path, context);
						if (loaded)
						{
							inspectedMesh = std::move(loaded.value.mesh);
							meshDiagnostic.clear();
						}
						else
						{
							meshDiagnostic = loaded.error;
						}
					}
					else
					{
						meshDiagnostic = session.error;
					}
				}
				if (inspectedMesh)
				{
					const auto data = inspectedMesh->Snapshot();
					ImGui::Text("Vertices: %llu  Indices: %llu  Submeshes: %llu", UInt64(data->data.positions.size()),
					            UInt64(data->data.indices.size()), UInt64(data->data.subMeshes.size()));
					ImGui::Text("Bounds min: %.3f, %.3f, %.3f", data->data.bounds.minimum.x,
					            data->data.bounds.minimum.y, data->data.bounds.minimum.z);
					ImGui::Text("Bounds max: %.3f, %.3f, %.3f", data->data.bounds.maximum.x,
					            data->data.bounds.maximum.y, data->data.bounds.maximum.z);
				}
				if (!meshDiagnostic.empty())
				{
					ImGui::TextWrapped("%s", meshDiagnostic.c_str());
				}
			}
		}
		else
		{
			ImGui::TextUnformatted("Type: Source asset");
		}
		if (const AssetDatabase* database = selection.assetLocalFileId ? nullptr : workspace.Database())
		{
			auto metadata = database->Metadata(asset->path);
			if (metadata && !metadata.value.importer.empty())
			{
				ImGui::Text("Importer: %s", metadata.value.importer.c_str());
				ImGui::Text("Importer Version: %u", metadata.value.importerVersion);
			}
		}
		if (!selection.assetLocalFileId && properties &&
		    (!asset->kind || (*asset->kind != ResourceKind::Material && *asset->kind != ResourceKind::Prefab)))
		{
			properties();
		}
		if (!footer && !asset->folder)
		{
			ImGui::TextDisabled("No preview is available for this asset.");
		}
		if (!selection.assetLocalFileId && !asset->folder && openAsset)
		{
			if (Icons::Button("Open"))
			{
				openAsset(*asset);
			}
		}
	}
	if (footer)
	{
		ImGui::EndChild();
		footer(maximumFooterHeight);
	}
	ImGui::End();
}
} // namespace Ursa::Editor
