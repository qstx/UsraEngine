#include <Serialization/AssetReference.hpp>
#include <Assets/ProjectBrowserPanel.hpp>

#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <functional>
#include <tuple>

namespace Ursa::Editor
{
Result<void> ProjectBrowserPanel::Draw(ProjectWorkspace& workspace, ProjectBrowserModel& model,
                                       EditorSelectionContext& selection, EditorHierarchy& hierarchy, bool& open,
                                       bool readOnly, String& diagnostic, const ProjectBrowserActions& actions)
{
	if (!open)
	{
		return {};
	}
	if (!ImGui::Begin("Project", &open))
	{
		ImGui::End();
		return {};
	}
	if (model.NeedsRefresh(workspace))
	{
		auto refreshed = model.Refresh(workspace);
		if (!refreshed)
		{
			diagnostic = refreshed.error;
		}
		else if (model.SelectedAsset().scheme.empty() && selection.kind == EditorSelectionKind::Asset)
		{
			selection.Clear();
		}
	}
	ImGui::TextWrapped("Assets: %s", PathToUrsaString(workspace.ProjectRoot()).c_str());
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	ImGui::InputTextWithHint("##NewAssetPath", "Asset path for Create/Rename", destinationPath.data(),
	                         destinationPath.size());
	ImGui::InputTextWithHint("##NewFolderName", "New folder name", folderName.data(), folderName.size());
	ImGui::BeginDisabled(readOnly || !actions.createFolder);
	if (ImGui::Button("Create Folder"))
	{
		auto created = actions.createFolder(String(folderName.data()));
		diagnostic = created ? String{} : created.error;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(readOnly || selection.kind != EditorSelectionKind::Asset || !actions.renameAsset);
	if (ImGui::Button("Rename Selected"))
	{
		auto renamed = RenameSelected(model, selection, actions);
		diagnostic = renamed ? String{} : renamed.error;
		if (renamed)
		{
			model.SelectAsset(selection.assetPath);
		}
	}
	ImGui::EndDisabled();
	if (renameRequested)
	{
		ImGui::OpenPopup("Rename Project Asset");
		renameRequested = false;
	}
	if (ImGui::BeginPopupModal("Rename Project Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Rename %s", selection.assetPath.FileName().c_str());
		ImGui::InputText("New path", destinationPath.data(), destinationPath.size());
		if (ImGui::Button("Rename") && actions.renameAsset)
		{
			auto renamed = RenameSelected(model, selection, actions);
			diagnostic = renamed ? String{} : renamed.error;
			if (renamed)
			{
				model.SelectAsset(selection.assetPath);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (actions.refresh && ImGui::SmallButton("Refresh"))
	{
		auto refreshed = actions.refresh();
		diagnostic = refreshed ? String{} : refreshed.error;
	}
	ImGui::Separator();
	ImGui::BeginChild("ProjectFolderTree", ImVec2(190.0f, 0.0f), ImGuiChildFlags_Borders);
	const String currentFolder = model.CurrentFolder();
	auto selectFolder = [&](const String& path)
	{
		auto navigated = model.NavigateTo(path);
		diagnostic = navigated ? String{} : navigated.error;
		if (navigated)
		{
			model.ClearAssetSelection();
			selection.Clear();
			hierarchy.ClearSelection();
		}
	};
	if (ImGui::TreeNodeEx("Assets", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
	                                    (currentFolder.empty() ? ImGuiTreeNodeFlags_Selected : 0)))
	{
		if (ImGui::IsItemClicked())
		{
			selectFolder({});
		}
		std::function<void(const ProjectBrowserEntry&)> drawFolder = [&](const ProjectBrowserEntry& folder)
		{
			ImGui::PushID(folder.path.relative.c_str());
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
			                           ImGuiTreeNodeFlags_OpenOnDoubleClick;
			if (folder.path.relative == currentFolder)
			{
				flags |= ImGuiTreeNodeFlags_Selected;
			}
			const bool folderOpen = ImGui::TreeNodeEx("##folder", flags, "%s", folder.name.c_str());
			if (ImGui::IsItemClicked())
			{
				selectFolder(folder.path.relative);
			}
			if (folderOpen)
			{
				for (const ProjectBrowserEntry& child : model.ChildFolders(folder.path.relative))
				{
					drawFolder(child);
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		};
		for (const ProjectBrowserEntry& folder : model.ChildFolders({}))
		{
			drawFolder(folder);
		}
		ImGui::TreePop();
	}
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("ProjectFolderContents", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	ImGui::BeginChild("ProjectBreadcrumbs", ImVec2(0.0f, 26.0f), ImGuiChildFlags_None);
	if (ImGui::SmallButton("Assets"))
	{
		selectFolder({});
	}
	String breadcrumbPath;
	Size cursor = 0;
	while (cursor < currentFolder.size())
	{
		const Size separator = currentFolder.find('/', cursor);
		const String part = currentFolder.substr(cursor, separator == String::npos ? String::npos : separator - cursor);
		breadcrumbPath = breadcrumbPath.empty() ? part : breadcrumbPath + "/" + part;
		ImGui::SameLine();
		if (ImGui::SmallButton(part.c_str()))
		{
			selectFolder(breadcrumbPath);
		}
		if (separator == String::npos)
		{
			break;
		}
		cursor = separator + 1;
	}
	ImGui::EndChild();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##ProjectSearch", "Search Assets", searchText.data(), searchText.size());
	const String search(searchText.data());
	if (search.empty())
	{
		for (const ProjectBrowserEntry& folder : model.ChildFolders(currentFolder))
		{
			ImGui::PushID(folder.path.relative.c_str());
			if (ImGui::Selectable((String("[Folder] ") + folder.name).c_str()))
			{
				selectFolder(folder.path.relative);
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				model.SelectAsset(folder.path);
				selection.SelectAsset(folder.path);
				hierarchy.ClearSelection();
			}
			if (ImGui::BeginPopupContextItem("FolderActions"))
			{
				if (ImGui::MenuItem("Open"))
				{
					selectFolder(folder.path.relative);
				}
				ImGui::BeginDisabled(readOnly);
				if (ImGui::BeginMenu("Create"))
				{
					if (ImGui::MenuItem("Folder") && actions.createFolder)
					{
						model.NavigateTo(folder.path.relative);
						auto created = actions.createFolder(String(folderName.data()));
						diagnostic = created ? String{} : created.error;
						folderName.fill(0);
					}
					for (const auto& item :
					     {std::tuple<const char*, ResourceKind, MaterialType>{"Scene", ResourceKind::Scene,
					                                                          MaterialType::Unlit},
					      {"Prefab", ResourceKind::Prefab, MaterialType::Unlit},
					      {"Unlit Material", ResourceKind::Material, MaterialType::Unlit},
					      {"PBR Material", ResourceKind::Material, MaterialType::PbrMetallicRoughness},
					      {"Texture", ResourceKind::Texture, MaterialType::Unlit}})
					{
						if (ImGui::MenuItem(std::get<0>(item)) && actions.createAsset)
						{
							model.NavigateTo(folder.path.relative);
							destinationPath.fill(0);
							auto created = actions.createAsset(std::get<1>(item), std::get<2>(item), String{});
							diagnostic = created ? String{} : created.error;
							selection.Clear();
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndDisabled();
				ImGui::BeginDisabled(!actions.renameAsset || readOnly);
				if (ImGui::MenuItem("Rename Selected"))
				{
					selection.SelectAsset(folder.path);
					model.SelectAsset(folder.path);
					renameRequested = true;
				}
				ImGui::EndDisabled();
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
	}
	for (const ProjectBrowserEntry& asset : model.ChildAssets(currentFolder, search))
	{
		ImGui::PushID(asset.path.relative.c_str());
		const bool selected = model.SelectedAsset() == asset.path;
		const String label = search.empty() ? asset.name : asset.path.relative;
		if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
		{
			model.SelectAsset(asset.path);
			selection.SelectAsset(asset.path);
			hierarchy.ClearSelection();
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && actions.openAsset)
			{
				auto opened = actions.openAsset(asset);
				diagnostic = opened ? String{} : opened.error;
			}
		}
		if (!asset.folder && ImGui::BeginDragDropSource())
		{
			const AssetReference reference{asset.guid, 0};
			ImGui::SetDragDropPayload("URSA_ASSET_REFERENCE", &reference, sizeof(reference));
			ImGui::TextUnformatted(asset.name.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			model.SelectAsset(asset.path);
			selection.SelectAsset(asset.path);
			hierarchy.ClearSelection();
		}
		if (ImGui::BeginPopupContextItem("AssetActions"))
		{
			if (ImGui::MenuItem("Open") && actions.openAsset)
			{
				auto opened = actions.openAsset(asset);
				diagnostic = opened ? String{} : opened.error;
			}
			ImGui::BeginDisabled(!actions.renameAsset || readOnly);
			if (ImGui::MenuItem("Rename Selected"))
			{
				renameRequested = true;
			}
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::End();
	return {};
}

Result<void> ProjectBrowserPanel::RenameSelected(const ProjectBrowserModel& model, EditorSelectionContext& selection,
                                                 const ProjectBrowserActions& actions)
{
	const VirtualPath& source = model.SelectedAsset();
	if (source.scheme.empty() || !actions.renameAsset)
	{
		return Result<void>::Failure("Select a writable Project asset to rename");
	}
	auto destination = VirtualPath::Parse(String(projectResourceScheme) + ":" + destinationPath.data());
	if (!destination)
	{
		return Result<void>::Failure(destination.error);
	}
	auto renamed = actions.renameAsset(source, destination.value);
	if (renamed)
	{
		selection.SelectAsset(destination.value);
	}
	return renamed;
}
} // namespace Ursa::Editor
