#include <Ui/EditorIcons.hpp>
#include <Serialization/AssetReference.hpp>
#include <Assets/ProjectBrowserPanel.hpp>

#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <algorithm>
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
	}
	model.ReconcileSelection(selection);
	Vector<const ScriptTypeDescriptor*> scriptableAssetTypes;
	if (actions.scriptableAssetTypes)
	{
		for (const ScriptTypeDescriptor& type : *actions.scriptableAssetTypes)
		{
			if (type.group == ScriptAssemblyGroup::Game && type.kind == ScriptTypeKind::ScriptableObject &&
			    !type.createAssetMenuName.empty())
			{
				scriptableAssetTypes.push_back(&type);
			}
		}
		std::sort(scriptableAssetTypes.begin(), scriptableAssetTypes.end(),
		          [](const ScriptTypeDescriptor* left, const ScriptTypeDescriptor* right)
		          {
			          return left->createAssetMenuOrder != right->createAssetMenuOrder
			                     ? left->createAssetMenuOrder < right->createAssetMenuOrder
			                     : left->createAssetMenuName < right->createAssetMenuName;
		          });
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
	if (Icons::Button("Create Folder"))
	{
		auto created = actions.createFolder(String(folderName.data()));
		diagnostic = created ? String{} : created.error;
	}
	ImGui::EndDisabled();
	Icons::SameLine();
	ImGui::BeginDisabled(readOnly || selection.kind != EditorSelectionKind::Asset || selection.assetMissing ||
	                     selection.assetLocalFileId || !actions.renameAsset);
	if (Icons::Button("Rename Selected"))
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
		if (Icons::Button("Rename") && actions.renameAsset)
		{
			auto renamed = RenameSelected(model, selection, actions);
			diagnostic = renamed ? String{} : renamed.error;
			if (renamed)
			{
				model.SelectAsset(selection.assetPath);
				ImGui::CloseCurrentPopup();
			}
		}
		Icons::SameLine();
		if (Icons::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (actions.refresh && Icons::Button("Refresh", Icons::Icon::Refresh, "Refresh assets (Ctrl+R)"))
	{
		auto refreshed = actions.refresh();
		diagnostic = refreshed ? String{} : refreshed.error;
	}
	ImGui::Separator();
	ImGui::BeginChild("ProjectFolderTree", ImVec2(190.0f, 0.0f), ImGuiChildFlags_Borders);
	const String currentFolder = model.CurrentFolder();
	auto selectFolder = [&](const String& path)
	{
		if (!selection.Clear())
		{
			return;
		}
		auto navigated = model.NavigateTo(path);
		diagnostic = navigated ? String{} : navigated.error;
		if (navigated)
		{
			model.ClearAssetSelection();
			hierarchy.ClearSelection();
		}
	};
	if (Icons::TreeNode("Assets",
	                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
	                        (currentFolder.empty() ? ImGuiTreeNodeFlags_Selected : 0),
	                    Icons::Icon::Folder, "Assets"))
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
			const bool folderOpen = Icons::TreeNode("##folder", flags, Icons::Icon::Folder, folder.name.c_str());
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
	Icons::SameLine();
	ImGui::BeginChild("ProjectFolderContents", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	const Size pathParts =
	    currentFolder.empty() ? 0 : 1 + static_cast<Size>(std::count(currentFolder.begin(), currentFolder.end(), '/'));
	const Float32 breadcrumbWidth = ImGui::CalcTextSize("Assets").x + ImGui::CalcTextSize(currentFolder.c_str()).x +
	                                static_cast<Float32>(pathParts + 1) * 48.f;
	ImGui::SetNextWindowContentSize({breadcrumbWidth, 0.f});
	const Float32 breadcrumbHeight =
	    std::max(28.f, ImGui::GetFrameHeight()) + ImGui::GetStyle().ItemSpacing.y + ImGui::GetStyle().ScrollbarSize;
	ImGui::BeginChild("ProjectBreadcrumbs", ImVec2(0.0f, breadcrumbHeight), ImGuiChildFlags_None,
	                  ImGuiWindowFlags_HorizontalScrollbar);
	if (Icons::Button("Assets", Icons::Icon::Folder, "Assets", false, true))
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
		Icons::SameLine();
		if (Icons::Button(part.c_str(), Icons::Icon::Folder, part.c_str(), false, true))
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
	Icons::Inline(Icons::Icon::Search);
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##ProjectSearch", "Search Assets", searchText.data(), searchText.size());
	const String search(searchText.data());
	if (search.empty())
	{
		for (const ProjectBrowserEntry& folder : model.ChildFolders(currentFolder))
		{
			ImGui::PushID(folder.path.relative.c_str());
			if (Icons::Selectable(Icons::Icon::Folder, folder.name.c_str()))
			{
				selectFolder(folder.path.relative);
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				if (selection.SelectAsset(folder.path))
				{
					model.SelectAsset(folder.path);
					hierarchy.ClearSelection();
				}
			}
			if (ImGui::BeginPopupContextItem("FolderActions"))
			{
				if (Icons::MenuItem("Open"))
				{
					selectFolder(folder.path.relative);
				}
				ImGui::BeginDisabled(readOnly);
				if (Icons::BeginMenu("Create"))
				{
					if (Icons::MenuItem("Folder") && actions.createFolder)
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
						if (Icons::MenuItem(std::get<0>(item)) && actions.createAsset)
						{
							model.NavigateTo(folder.path.relative);
							destinationPath.fill(0);
							auto created = actions.createAsset(std::get<1>(item), std::get<2>(item), String{});
							diagnostic = created ? String{} : created.error;
							selection.Clear();
						}
					}
					for (const ScriptTypeDescriptor* type : scriptableAssetTypes)
					{
						if (Icons::MenuItem(type->createAssetMenuName.c_str()) && actions.createScriptableAsset)
						{
							model.NavigateTo(folder.path.relative);
							auto created = actions.createScriptableAsset(*type, type->createAssetFileName);
							diagnostic = created ? String{} : created.error;
							if (created)
							{
								model.Refresh(workspace);
							}
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndDisabled();
				ImGui::BeginDisabled(!actions.renameAsset || readOnly);
				if (Icons::MenuItem("Rename Selected"))
				{
					if (selection.SelectAsset(folder.path))
					{
						model.SelectAsset(folder.path);
						renameRequested = true;
					}
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
		bool& expanded = expandedAssets[asset.guid.ToString()];
		const auto visibleObjects = asset.VisibleChildren();
		const bool hasChildren = !visibleObjects.empty();
		if (hasChildren)
		{
			if (ImGui::ArrowButton("##ImportedObjects", expanded ? ImGuiDir_Down : ImGuiDir_Right))
			{
				expanded = !expanded;
			}
			ImGui::SameLine();
		}
		const bool selected = selection.kind == EditorSelectionKind::Asset && !selection.assetMissing &&
		                      selection.assetPath == asset.path && !selection.assetLocalFileId;
		const String label = search.empty() ? asset.name : asset.path.relative;
		if (Icons::Selectable(Icons::ForAsset(false, asset.kind, asset.importer, asset.path.relative), label.c_str(),
		                      selected, ImGuiSelectableFlags_AllowDoubleClick))
		{
			const bool accepted = selection.SelectAsset(asset.path, 0, {}, asset.guid);
			if (accepted)
			{
				model.SelectAsset(asset.path);
				hierarchy.ClearSelection();
			}
			if (accepted && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && actions.openAsset)
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
			if (selection.SelectAsset(asset.path, 0, {}, asset.guid))
			{
				model.SelectAsset(asset.path);
				hierarchy.ClearSelection();
			}
		}
		if (ImGui::BeginPopupContextItem("AssetActions"))
		{
			if (Icons::MenuItem("Open") && actions.openAsset && selection.SelectAsset(asset.path, 0, {}, asset.guid))
			{
				model.SelectAsset(asset.path);
				hierarchy.ClearSelection();
				auto opened = actions.openAsset(asset);
				diagnostic = opened ? String{} : opened.error;
			}
			ImGui::BeginDisabled(!actions.renameAsset || readOnly || selection.kind != EditorSelectionKind::Asset ||
			                     selection.assetMissing || selection.assetPath != asset.path ||
			                     selection.assetLocalFileId);
			if (Icons::MenuItem("Rename Selected"))
			{
				renameRequested = true;
			}
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}
		if (expanded || !search.empty())
		{
			ImGui::Indent();
			for (const auto& object : visibleObjects)
			{
				if (object.isMainObject || !object.visibleInProject)
				{
					continue;
				}
				const String localId = ToUrsaString(std::to_string(object.localFileId));
				ImGui::PushID(localId.c_str());
				const String objectLabel = object.Label();
				const auto resourceKind = ResourceKindForPath(object.path);
				if (Icons::Selectable(
				        Icons::ForAsset(false,
				                        resourceKind ? std::optional<ResourceKind>(resourceKind.value) : std::nullopt,
				                        {}, {}),
				        objectLabel.c_str(),
				        selection.kind == EditorSelectionKind::Asset && selection.assetPath == asset.path &&
				            selection.assetLocalFileId == object.localFileId))
				{
					if (selection.SelectAsset(asset.path, object.localFileId, object.kind, asset.guid))
					{
						model.SelectAsset(asset.path);
						hierarchy.ClearSelection();
					}
				}
				if (ImGui::BeginDragDropSource())
				{
					const AssetReference reference{asset.guid, object.localFileId};
					ImGui::SetDragDropPayload("URSA_ASSET_REFERENCE", &reference, sizeof(reference));
					ImGui::TextUnformatted(objectLabel.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right) &&
				    selection.SelectAsset(asset.path, object.localFileId, object.kind, asset.guid))
				{
					model.SelectAsset(asset.path);
					hierarchy.ClearSelection();
				}
				if (ImGui::BeginPopupContextItem("ImportedObjectActions"))
				{
					if (Icons::MenuItem("Select Source") && selection.SelectAsset(asset.path, 0, {}, asset.guid))
					{
						model.SelectAsset(asset.path);
						hierarchy.ClearSelection();
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			ImGui::Unindent();
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
	if (source.scheme.empty() || selection.assetMissing || selection.assetLocalFileId || !actions.renameAsset)
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
