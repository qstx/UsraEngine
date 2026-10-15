#include <Inspector/AssetInspectorPanel.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Serialization/AssetReference.hpp>
#include <Serialization/MeshResource.hpp>
#include <Workspace/ProjectWorkspace.hpp>
#include <Core/StringInterop.hpp>

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Ursa::Editor
{
namespace
{
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

bool EndsWith(const String& value, const char* suffix)
{
	const Size length = std::strlen(suffix);
	return value.size() >= length && value.compare(value.size() - length, length, suffix) == 0;
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

nlohmann::json DefaultValue(const String& kind)
{
	if (kind == "Boolean")
	{
		return false;
	}
	if (kind == "SignedInteger")
	{
		return Int64{0};
	}
	if (kind == "UnsignedInteger")
	{
		return UInt64{0};
	}
	if (kind == "FloatingPoint")
	{
		return 0.0;
	}
	if (kind == "String")
	{
		return "";
	}
	if (kind == "Vector2")
	{
		return nlohmann::json::array({0.0, 0.0});
	}
	if (kind == "Vector3")
	{
		return nlohmann::json::array({0.0, 0.0, 0.0});
	}
	if (kind == "Vector4" || kind == "Quaternion" || kind == "Color")
	{
		return nlohmann::json::array({0.0, 0.0, 0.0, 1.0});
	}
	if (kind == "Array" || kind == "List")
	{
		return nlohmann::json::array();
	}
	return nullptr;
}

ResourceKind ExpectedResourceKind(const String& kind)
{
	if (kind == "MeshReference")
	{
		return ResourceKind::Mesh;
	}
	if (kind == "MaterialReference")
	{
		return ResourceKind::Material;
	}
	if (kind == "TextureReference")
	{
		return ResourceKind::Texture;
	}
	return ResourceKind::Shader;
}

String ReferenceKindName(const String& kind)
{
	if (kind == "MeshReference")
	{
		return "Mesh";
	}
	if (kind == "MaterialReference")
	{
		return "Material";
	}
	if (kind == "TextureReference")
	{
		return "Texture";
	}
	if (kind == "ShaderReference")
	{
		return "Shader";
	}
	if (kind == "ComputeShaderReference")
	{
		return "ComputeShader";
	}
	if (kind == "ScriptableObjectReference")
	{
		return "ScriptableObject";
	}
	return "Asset";
}

nlohmann::json ReferenceValue(const String& kind, AssetReference reference)
{
	if (kind == "AssetReference")
	{
		return AssetReferenceToJson(reference);
	}
	const String referenceKind = ReferenceKindName(kind);
	nlohmann::json resource = AssetReferenceToJson(reference);
	if (kind == "MeshReference" || kind == "MaterialReference")
	{
		resource = {{"kind", "asset"}, {"asset", std::move(resource)}};
	}
	return {{"$ursaReference", {{"Kind", ToStdString(referenceKind)}, {"Id", ""}, {"Resource", resource}}}};
}

ResourceId CurrentReferenceGuid(const nlohmann::json& value)
{
	try
	{
		const nlohmann::json* identity = &value;
		if (value.contains("$ursaReference"))
		{
			identity = &value.at("$ursaReference").at("Resource");
			if (identity->contains("asset"))
			{
				identity = &identity->at("asset");
			}
		}
		if (identity->contains("guid") && identity->at("guid").is_string())
		{
			auto id = ResourceId::Parse(ToUrsaString(identity->at("guid").get<std::string>()));
			return id ? id.value : ResourceId{};
		}
	}
	catch (const std::exception&)
	{
	}
	return {};
}

bool DrawReference(nlohmann::json& value, const ScriptFieldTypeDescriptor& field, ProjectWorkspace& workspace)
{
	const String& kind = field.kind;
	const ResourceId selected = CurrentReferenceGuid(value);
	String preview = selected ? selected.ToString() : "None";
	for (const ResourceEntry& entry : workspace.Resources())
	{
		if (entry.id == selected)
		{
			preview = entry.location.path.ToString();
			break;
		}
	}
	bool changed = false;
	if (!ImGui::BeginCombo("##Reference", preview.c_str()))
	{
		return false;
	}
	if (ImGui::Selectable("None", !selected))
	{
		value = nullptr;
		changed = true;
	}
	const auto select = [&](AssetReference reference, const String& label)
	{
		const String item =
		    label + "##" + reference.guid.ToString() + ":" + ToUrsaString(std::to_string(reference.localFileId));
		if (ImGui::Selectable(item.c_str(), reference.guid == selected))
		{
			value = ReferenceValue(kind, reference);
			changed = true;
		}
	};
	if (kind == "ScriptableObjectReference")
	{
		if (AssetDatabase* database = workspace.Database())
		{
			for (const AssetRecord& record : database->Assets())
			{
				if (record.metadata.folder || !IsScriptableAsset(record.path))
				{
					continue;
				}
				auto asset = LoadScriptableAssetSource(database->Filesystem(), record.path, record.metadata.guid);
				if (asset)
				{
					select({record.metadata.guid, asset.value.LocalFileId()}, record.path.ToString());
				}
			}
		}
	}
	else if (kind == "ComputeShaderReference")
	{
		for (const AssetDatabase* database : {workspace.Database(), workspace.BuiltinDatabase()})
		{
			if (!database)
			{
				continue;
			}
			for (const ImportedArtifactRecord& artifact : database->ImportedArtifacts())
			{
				if (artifact.kind == "ComputeShader")
				{
					select({artifact.guid, artifact.localFileId}, artifact.path.ToString());
				}
			}
		}
	}
	else if (kind == "MeshReference" || kind == "MaterialReference")
	{
		const ResourceKind expected = ExpectedResourceKind(kind);
		for (const ResourceEntry& entry : workspace.Resources())
		{
			if (entry.location.kind == expected)
			{
				select({entry.id, 0}, entry.location.path.ToString());
			}
		}
		for (const AssetDatabase* database : {workspace.Database(), workspace.BuiltinDatabase()})
		{
			if (!database)
			{
				continue;
			}
			for (const ImportedArtifactRecord& artifact : database->ImportedArtifacts())
			{
				const String expectedArtifact = kind == "MeshReference" ? "Mesh" : "Material";
				if (artifact.kind == expectedArtifact && artifact.localFileId)
				{
					select({artifact.guid, artifact.localFileId}, artifact.path.ToString());
				}
			}
		}
	}
	else
	{
		ResourceKind expected = ExpectedResourceKind(kind);
		for (const ResourceEntry& entry : workspace.Resources())
		{
			if (kind == "AssetReference" || entry.location.kind == expected)
			{
				select({entry.id, 0}, entry.location.path.ToString());
			}
		}
	}
	ImGui::EndCombo();
	return changed;
}

bool DrawFieldValue(nlohmann::json& value, const ScriptFieldTypeDescriptor& field, ProjectWorkspace& workspace)
{
	const String& kind = field.kind;
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
		Array<char, 1024> candidate{};
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
	else if (kind == "Enumeration")
	{
		String preview = ToUrsaString(value.dump());
		for (Size index = 0; index < field.enumValues.size() && index < field.enumNumericValuesJson.size(); ++index)
		{
			try
			{
				if (nlohmann::json::parse(ToStdString(field.enumNumericValuesJson[index])) == value)
				{
					preview = field.enumValues[index];
				}
			}
			catch (const std::exception&)
			{
			}
		}
		if (ImGui::BeginCombo("##Value", preview.c_str()))
		{
			for (Size index = 0; index < field.enumValues.size() && index < field.enumNumericValuesJson.size(); ++index)
			{
				auto numeric = nlohmann::json::parse(ToStdString(field.enumNumericValuesJson[index]));
				if (ImGui::Selectable(field.enumValues[index].c_str(), numeric == value))
				{
					value = std::move(numeric);
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
		Float32 components[4]{0.0f, 0.0f, 0.0f, kind == "Quaternion" || kind == "Color" ? 1.0f : 0.0f};
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
	else if (EndsWith(kind, "Reference"))
	{
		return DrawReference(value, field, workspace);
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
			ScriptFieldTypeDescriptor element = field;
			element.kind = field.elementKind;
			ImGui::SetNextItemWidth(-42.0f);
			changed |= DrawFieldValue(value[index], element, workspace);
			ImGui::SameLine();
			if (ImGui::SmallButton("-"))
			{
				value.erase(value.begin() + static_cast<std::ptrdiff_t>(index));
				changed = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (ImGui::SmallButton("Add element"))
		{
			value.push_back(DefaultValue(field.elementKind));
			changed = true;
		}
		return changed;
	}
	else
	{
		ImGui::TextDisabled("Unsupported field type: %s", field.typeName.c_str());
	}
	return false;
}

bool ValidateFieldValue(const nlohmann::json& value, const ScriptFieldTypeDescriptor& field)
{
	if (field.kind == "Array" || field.kind == "List")
	{
		if (!value.is_array())
		{
			return false;
		}
		ScriptFieldTypeDescriptor element = field;
		element.kind = field.elementKind;
		return std::all_of(value.begin(), value.end(),
		                   [&](const nlohmann::json& item) { return ValidateFieldValue(item, element); });
	}
	if (value.is_null())
	{
		return field.kind == "String" || EndsWith(field.kind, "Reference");
	}
	if (field.kind == "Boolean")
	{
		return value.is_boolean();
	}
	if (field.kind == "SignedInteger")
	{
		return value.is_number_integer();
	}
	if (field.kind == "Enumeration")
	{
		return (value.is_number_integer() || value.is_number_unsigned()) &&
		       std::any_of(field.enumNumericValuesJson.begin(), field.enumNumericValuesJson.end(),
		                   [&](const String& item)
		                   {
			                   try
			                   {
				                   return nlohmann::json::parse(ToStdString(item)) == value;
			                   }
			                   catch (const std::exception&)
			                   {
				                   return false;
			                   }
		                   });
	}
	if (field.kind == "UnsignedInteger")
	{
		return value.is_number_unsigned() || (value.is_number_integer() && value.get<Int64>() >= 0);
	}
	if (field.kind == "FloatingPoint")
	{
		return value.is_number() && std::isfinite(value.get<Float64>());
	}
	if (field.kind == "String")
	{
		return value.is_string();
	}
	if (field.kind == "Vector2")
	{
		return value.is_array() && value.size() == 2;
	}
	if (field.kind == "Vector3")
	{
		return value.is_array() && value.size() == 3;
	}
	if (field.kind == "Vector4" || field.kind == "Quaternion" || field.kind == "Color")
	{
		return value.is_array() && value.size() == 4;
	}
	if (field.kind == "AssetReference")
	{
		try
		{
			if (!value.is_object() || !value.contains("guid") || !value.at("guid").is_string() ||
			    !value.contains("localFileId") || !value.at("localFileId").is_number_unsigned())
			{
				return false;
			}
			auto id = ResourceId::Parse(ToUrsaString(value.at("guid").get<std::string>()));
			return id && bool(id.value);
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
	if (EndsWith(field.kind, "Reference"))
	{
		try
		{
			if (!value.is_object() || !value.contains("$ursaReference"))
			{
				return false;
			}
			const nlohmann::json& reference = value.at("$ursaReference");
			if (!reference.is_object() ||
			    reference.value("Kind", std::string{}) != ToStdString(ReferenceKindName(field.kind)))
			{
				return false;
			}
			if (field.kind == "GameObjectReference" || field.kind == "ComponentReference")
			{
				return reference.value("Id", std::string{}).empty();
			}
			if (!reference.contains("Resource") || !reference.at("Resource").is_object())
			{
				return false;
			}
			const nlohmann::json* identity = &reference.at("Resource");
			if (field.kind == "MeshReference" || field.kind == "MaterialReference")
			{
				if (identity->value("kind", std::string{}) != "asset" || !identity->contains("asset"))
				{
					return false;
				}
				identity = &identity->at("asset");
			}
			if (!identity->contains("guid") || !identity->at("guid").is_string() ||
			    !identity->contains("localFileId") || !identity->at("localFileId").is_number_unsigned())
			{
				return false;
			}
			auto id = ResourceId::Parse(ToUrsaString(identity->at("guid").get<std::string>()));
			return id && bool(id.value) &&
			       (field.kind != "ScriptableObjectReference" || identity->at("localFileId").get<UInt64>() != 0);
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
	return false;
}

} // namespace

void AssetInspectorPanel::Draw(ProjectWorkspace& workspace, const ProjectBrowserModel& model,
                               const EditorSelectionContext& selection, bool& open,
                               const Vector<ScriptTypeDescriptor>* scriptTypes, const OpenAsset& openAsset,
                               const RequestImport& requestImport)
{
	if (!open || selection.kind != EditorSelectionKind::Asset)
	{
		return;
	}
	if (!ImGui::Begin("Inspector", &open))
	{
		ImGui::End();
		return;
	}
	const ProjectBrowserEntry* asset = model.Find(selection.assetPath);
	if (!asset)
	{
		ImGui::TextDisabled("The selected asset is no longer available.");
	}
	else
	{
		ImGui::TextUnformatted(asset->name.c_str());
		ImGui::Separator();
		ImGui::Text("Path: %s", asset->path.ToString().c_str());
		ImGui::Text("GUID: %s", asset->guid.ToString().c_str());
		if (asset->folder)
		{
			ImGui::TextUnformatted("Type: Folder");
		}
		else if (IsScriptableAsset(asset->path))
		{
			ImGui::TextUnformatted("Type: ScriptableObject");
			const String key = asset->path.ToString() + ":" + asset->guid.ToString();
			if (key != inspectedScriptableKey)
			{
				inspectedScriptableKey = key;
				scriptableLoaded = false;
				scriptableDirty = false;
				scriptablePayload = nlohmann::json::object();
				scriptableDiagnostic.clear();
				auto loaded = LoadScriptableAssetSource(workspace.Index().Filesystem(), asset->path, asset->guid);
				if (loaded)
				{
					scriptableAsset = std::move(loaded.value);
					scriptablePayload = scriptableAsset.Payload();
					scriptableLoaded = true;
				}
				else
				{
					scriptableDiagnostic = loaded.error;
				}
			}
			if (scriptableLoaded)
			{
				ImGui::Text("Script Type: %s", scriptableAsset.TypeId().c_str());
				ImGui::Text("Type Version: %u", scriptableAsset.TypeVersion());
				const ScriptTypeDescriptor* type = FindScriptType(scriptTypes, scriptableAsset.TypeId());
				const auto& payload = scriptableAsset.Payload();
				const bool currentTypeIdentity =
				    type && payload.contains("SourceGuid") && payload["SourceGuid"].is_string() &&
				    payload["SourceGuid"].get<std::string>() == ToStdString(type->sourceGuid.ToString()) &&
				    payload.contains("LocalFileId") && payload["LocalFileId"].is_number_unsigned() &&
				    payload["LocalFileId"].get<UInt64>() == type->localFileId;
				if (!type || type->kind != ScriptTypeKind::ScriptableObject ||
				    type->group != ScriptAssemblyGroup::Game || type->version != scriptableAsset.TypeVersion() ||
				    !currentTypeIdentity)
				{
					ImGui::TextWrapped("Missing ScriptableObject type. Source GUID: %s; local file ID: %llu. The "
					                   "original serialized fields are preserved.",
					                   scriptableAsset.SourceGuid().ToString().c_str(),
					                   static_cast<unsigned long long>(scriptableAsset.LocalFileId()));
					ImGui::TextDisabled("Fields: %s",
					                    scriptablePayload.value("Fields", nlohmann::json::object()).dump(2).c_str());
				}
				else
				{
					nlohmann::json& fields = scriptablePayload["Fields"];
					if (!fields.is_object())
					{
						scriptableDiagnostic = "ScriptableObject Fields payload must be an object";
					}
					else
					{
						for (const ScriptFieldTypeDescriptor& field : type->fields)
						{
							const String& name = field.name;
							auto value = fields.find(ToStdString(name));
							if (value == fields.end())
							{
								auto alias = std::find_if(field.formerNames.begin(), field.formerNames.end(),
								                          [&](const String& item)
								                          { return fields.contains(ToStdString(item)); });
								if (alias != field.formerNames.end())
								{
									value = fields.find(ToStdString(*alias));
								}
							}
							nlohmann::json candidate = value == fields.end()
							                               ? nlohmann::json::parse(ToStdString(field.defaultValueJson))
							                               : *value;
							ImGui::PushID(field.name.c_str());
							ImGui::AlignTextToFramePadding();
							ImGui::TextUnformatted(field.name.c_str());
							ImGui::SameLine(150.0f);
							ImGui::SetNextItemWidth(-1.0f);
							if (DrawFieldValue(candidate, field, workspace))
							{
								fields[ToStdString(name)] = std::move(candidate);
								scriptablePayload["FieldTypes"][ToStdString(name)] = {
								    {"Kind", ToStdString(field.kind)},
								    {"ElementKind", ToStdString(field.elementKind)},
								    {"TypeName", ToStdString(field.typeName)},
								    {"ElementTypeName", ToStdString(field.elementTypeName)}};
								scriptableDirty = true;
							}
							ImGui::PopID();
						}
					}
					if (scriptableDirty)
					{
						ImGui::TextDisabled("Dirty");
					}
					const bool writable = static_cast<bool>(workspace.CheckWritable(asset->path));
					ImGui::BeginDisabled(!scriptableDirty || !writable);
					if (ImGui::Button("Save ScriptableObject"))
					{
						bool valid = true;
						for (const ScriptFieldTypeDescriptor& field : type->fields)
						{
							auto value = fields.find(ToStdString(field.name));
							if (value != fields.end() && !ValidateFieldValue(*value, field))
							{
								scriptableDiagnostic = "Invalid value for " + type->clrTypeName + "." + field.name;
								valid = false;
								break;
							}
						}
						if (valid)
						{
							scriptablePayload["FormatVersion"] = 1;
							scriptablePayload["ScriptType"] = ToStdString(type->scriptType);
							scriptablePayload["Version"] = type->version;
							scriptablePayload["SourceGuid"] = ToStdString(type->sourceGuid.ToString());
							scriptablePayload["LocalFileId"] = type->localFileId;
							if (!scriptablePayload.contains("FieldTypes") ||
							    !scriptablePayload["FieldTypes"].is_object())
							{
								scriptablePayload["FieldTypes"] = nlohmann::json::object();
							}
							for (const ScriptFieldTypeDescriptor& field : type->fields)
							{
								scriptablePayload["FieldTypes"][ToStdString(field.name)] = {
								    {"Kind", ToStdString(field.kind)},
								    {"ElementKind", ToStdString(field.elementKind)},
								    {"TypeName", ToStdString(field.typeName)},
								    {"ElementTypeName", ToStdString(field.elementTypeName)}};
							}
							ScriptableAsset candidate;
							auto described = candidate.SetDescription(asset->guid, type->localFileId, type->scriptType,
							                                          type->version, scriptablePayload);
							if (!described)
							{
								scriptableDiagnostic = described.error;
							}
							else
							{
								auto saved =
								    SaveScriptableAssetSource(workspace.Index().Filesystem(), asset->path, candidate);
								if (!saved)
								{
									scriptableDiagnostic = saved.error;
								}
								else
								{
									scriptableAsset = std::move(candidate);
									scriptableDirty = false;
									scriptableDiagnostic.clear();
									if (requestImport)
									{
										requestImport(asset->path);
									}
								}
							}
						}
					}
					ImGui::EndDisabled();
				}
			}
			if (!scriptableDiagnostic.empty())
			{
				ImGui::TextWrapped("%s", scriptableDiagnostic.c_str());
			}
		}
		else if (asset->kind)
		{
			ImGui::Text("Type: %s", AssetKindLabel(*asset->kind));
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
		if (const AssetDatabase* database = workspace.Database())
		{
			auto metadata = database->Metadata(asset->path);
			if (metadata && !metadata.value.importer.empty())
			{
				ImGui::Text("Importer: %s", metadata.value.importer.c_str());
				ImGui::Text("Importer Version: %u", metadata.value.importerVersion);
			}
		}
		if (!asset->folder && openAsset)
		{
			if (ImGui::Button("Open"))
			{
				openAsset(*asset);
			}
		}
	}
	ImGui::End();
}
} // namespace Ursa::Editor
