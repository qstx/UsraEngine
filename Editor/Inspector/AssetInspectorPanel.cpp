#include <Ui/EditorIcons.hpp>
#include <Inspector/AssetInspectorPanel.hpp>
#include <Assets/ScriptableAssetInspector.hpp>
#include <Assets/ImportedAssetInspector.hpp>
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

} // namespace

bool AssetInspectorPanel::HasModifiedSource() const
{
	return (nativeInspector && nativeInspector->Active() && nativeInspector->OwnsDocument() &&
	        nativeInspector->Document().Dirty()) ||
	       (scriptableInspector && scriptableInspector->Dirty());
}

VirtualPath AssetInspectorPanel::ModifiedSourcePath() const
{
	if (nativeInspector && nativeInspector->Active() && nativeInspector->OwnsDocument() &&
	    nativeInspector->Document().Dirty())
	{
		return nativeInspector->Document().Path();
	}
	return scriptableInspector && scriptableInspector->Dirty() ? scriptableInspector->SourcePath() : VirtualPath{};
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
	ImGui::Text("Save changes to %s before leaving this asset?", path.ToString().c_str());
	auto result = ImporterSelectionResult::Pending;
	ImGui::BeginDisabled(!canSave);
	if (Icons::Button("Save"))
	{
		const auto saved = nativeInspector && nativeInspector->Active() && nativeInspector->OwnsDocument() &&
		                           nativeInspector->Document().Dirty()
		                       ? nativeInspector->Save()
		                   : scriptableInspector ? scriptableInspector->Save()
		                                         : Result<void>::Failure("No source asset draft is active");
		if (saved)
		{
			if (requestImport)
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
	if (selection.kind != EditorSelectionKind::Asset || selection.assetLocalFileId)
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
	return selection.kind == EditorSelectionKind::Asset && !selection.assetLocalFileId && nativeInspector &&
	               nativeInspector->Active() && nativeInspector->Document().Path() == selection.assetPath
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
	if (!ImGui::Begin("Inspector", &open, flags))
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
	const ProjectBrowserEntry* asset = model.Find(selection.assetPath);
	if (!asset)
	{
		ImGui::TextDisabled("The selected asset is no longer available.");
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
				ImGui::Text("Object: %s %llu", artifact->kind.c_str(), artifact->localFileId);
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
				scriptableInspector->Draw(requestImport, custom);
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
