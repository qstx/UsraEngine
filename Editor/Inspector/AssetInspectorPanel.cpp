#include <Inspector/AssetInspectorPanel.hpp>
#include <Serialization/MeshResource.hpp>
#include <Core/StringInterop.hpp>

#include <imgui.h>

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
} // namespace

void AssetInspectorPanel::Draw(const ProjectWorkspace& workspace, const ProjectBrowserModel& model,
                               const EditorSelectionContext& selection, bool& open, const OpenAsset& openAsset)
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
		else if (asset->kind)
		{
			ImGui::Text("Type: %s", AssetKindLabel(*asset->kind));
			if (*asset->kind == ResourceKind::Mesh)
			{
				const auto revision = workspace.Database() ? workspace.Database()->Revision() : 0;
				const String key = PathToUrsaString(workspace.ProjectRoot()) + ":" + asset->path.ToString() + ":" +
				                   asset->guid.ToString() + ":" + ToUrsaString(std::to_string(revision));
				if (key != inspectedMeshKey)
				{
					inspectedMeshKey = key;
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
