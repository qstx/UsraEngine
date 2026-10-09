#include <Inspector/AssetInspectorPanel.hpp>

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
