#include <Ui/EditorIcons.hpp>
#include <Serialization/AssetReference.hpp>
#include <Assets/ProjectBrowserPanel.hpp>
#include <Workspace/EditorDragSession.hpp>

#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <functional>
#include <tuple>

namespace Ursa::Editor
{
namespace
{
EditorSelectionEntry AssetSelection(const ProjectBrowserEntry& asset, UInt64 localFileId = 0,
                                    const String& resourceKind = {})
{
	EditorSelectionEntry entry;
	entry.kind = EditorSelectionKind::Asset;
	entry.assetPath = asset.path;
	entry.assetGuid = asset.guid;
	entry.assetLocalFileId = localFileId;
	entry.assetKind = resourceKind;
	entry.assetRole = localFileId ? EditorAssetSelectionRole::ImportedObject : EditorAssetSelectionRole::Source;
	return entry;
}

void DrawProjectAssetDragSource(const EditorSelectionContext& selection, const EditorSelectionEntry& entry,
                                const String& label)
{
	if (ImGui::BeginDragDropSource())
	{
		const auto drag = BeginEditorDragSession(selection, entry);
		if (drag)
		{
			ImGui::SetDragDropPayload(editorDragSessionPayloadType, &drag.value, sizeof(drag.value));
		}
		ImGui::TextUnformatted(label.c_str());
		ImGui::EndDragDropSource();
	}
}

void DrawProjectMoveDropTarget(const ImRect& rect, ProjectWorkspace& workspace, ProjectBrowserModel& model,
                               EditorSelectionContext& selection, const String& destinationFolder, bool readOnly,
                               String& diagnostic, const ProjectBrowserActions& actions)
{
	if (!workspace.Database() || rect.GetHeight() < 2.0f ||
	    !ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID("##projectMoveDropTarget")))
	{
		return;
	}
	if (const ImGuiPayload* payload =
	        ImGui::AcceptDragDropPayload(editorDragSessionPayloadType, ImGuiDragDropFlags_AcceptNoDrawDefaultRect |
	                                                                       ImGuiDragDropFlags_AcceptBeforeDelivery))
	{
		auto decoded = DecodeEditorDragPayload(payload->DataType, payload->Data, static_cast<Size>(payload->DataSize),
		                                       selection.revision);
		if (!decoded)
		{
			if (payload->IsDelivery())
			{
				diagnostic = decoded.error;
			}
		}
		else
		{
			const bool sceneObjects = !decoded.value.sources.empty() &&
			                          std::all_of(decoded.value.sources.begin(), decoded.value.sources.end(),
			                                      [](const EditorDragSource& source)
			                                      {
				                                      return source.identity.kind == EditorSelectionKind::GameObject &&
				                                             !source.identity.IsMissing();
			                                      });
			if (sceneObjects)
			{
				if (!readOnly && actions.exportScenePrefabs)
				{
					ImGui::GetWindowDrawList()->AddRectFilled(rect.Min, rect.Max,
					                                          ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.22f));
					if (ClaimEditorDragDropDelivery(payload))
					{
						auto exported = actions.exportScenePrefabs(decoded.value, destinationFolder);
						diagnostic = exported ? String{} : exported.error;
					}
				}
				else if (payload->IsDelivery())
				{
					diagnostic = "Drop Scene GameObjects on a writable Project folder to create Prefabs";
				}
			}
			else
			{
				auto plan = PlanProjectFolderDrop(*workspace.Database(), decoded.value, destinationFolder, readOnly);
				if (plan)
				{
					ImGui::GetWindowDrawList()->AddRectFilled(rect.Min, rect.Max,
					                                          ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.3f));
					if (ClaimEditorDragDropDelivery(payload))
					{
						auto moved = actions.moveAssets ? actions.moveAssets(plan.value)
						                                : Result<void>::Failure("Project Move is unavailable");
						if (moved)
						{
							auto rebuilt = model.Refresh(workspace);
							if (!rebuilt)
							{
								diagnostic = rebuilt.error;
							}
							else
							{
								model.ReconcileSelection(selection);
								diagnostic.clear();
							}
						}
						else
						{
							diagnostic = moved.error;
						}
					}
				}
				else if (payload->IsDelivery())
				{
					diagnostic = plan.error;
				}
			}
		}
	}
	ImGui::EndDragDropTarget();
}

void DrawScenePrefabDropTarget(const ImRect& rect, const String& destinationFolder, bool readOnly,
                               UInt64 selectionRevision, String& diagnostic, const ProjectBrowserActions& actions)
{
	const ImGuiPayload* active = ImGui::GetDragDropPayload();
	if (readOnly || !actions.exportScenePrefabs || !active || !active->IsDataType(editorDragSessionPayloadType) ||
	    rect.GetHeight() < 2.0f)
	{
		return;
	}
	auto drag =
	    DecodeEditorDragPayload(active->DataType, active->Data, static_cast<Size>(active->DataSize), selectionRevision);
	if (!drag || drag.value.sources.empty() ||
	    std::any_of(
	        drag.value.sources.begin(), drag.value.sources.end(), [](const EditorDragSource& source)
	        { return source.identity.kind != EditorSelectionKind::GameObject || source.identity.IsMissing(); }) ||
	    !ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID("##projectPrefabDropTarget")))
	{
		return;
	}
	if (const ImGuiPayload* payload =
	        ImGui::AcceptDragDropPayload(editorDragSessionPayloadType, ImGuiDragDropFlags_AcceptNoDrawDefaultRect |
	                                                                       ImGuiDragDropFlags_AcceptBeforeDelivery))
	{
		ImGui::GetWindowDrawList()->AddRectFilled(rect.Min, rect.Max,
		                                          ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.22f));
		if (ClaimEditorDragDropDelivery(payload))
		{
			auto exported = actions.exportScenePrefabs(drag.value, destinationFolder);
			diagnostic = exported ? String{} : exported.error;
		}
	}
	ImGui::EndDragDropTarget();
}
} // namespace

void ProjectBrowserPanel::Reset()
{
	expandedAssets.clear();
	searchText.fill(0);
	destinationPath.fill(0);
	folderName.fill(0);
	createFolderRequested = false;
	renameRequested = false;
	deleteRequested = false;
	deleteTargets.clear();
	backgroundMenuFolder.clear();
	contextMenuTargets.clear();
}

void ProjectBrowserPanel::RequestRenameSelected()
{
	renameRequested = true;
}

void ProjectBrowserPanel::RequestDeleteSelected(const EditorSelectionContext& selection)
{
	deleteTargets = selection.entries;
	deleteRequested = true;
	deleteDiagnostic.clear();
}

void ProjectBrowserPanel::CaptureMenuContext(const String& key, const EditorSelectionContext& selection)
{
	contextMenuTargets.clear();
	contextMenuTargets.emplace(
	    key, EditorCommandContext::Capture(selection, selection.activeDocumentId, selection.revision));
}

Result<void> ProjectBrowserPanel::CanExecuteMenuCommand(const String& key, const String& commandId, const String& label,
                                                        const EditorSelectionContext& selection,
                                                        const EditorCommandAvailability& availability) const
{
	const auto captured = contextMenuTargets.find(key);
	if (captured == contextMenuTargets.end())
	{
		return Result<void>::Failure("The Project context menu target is no longer available");
	}
	EditorCommands commands;
	auto registered =
	    commands.Register({commandId, label, {}, availability, [](const auto&) { return Result<void>{}; }});
	if (!registered)
	{
		return registered;
	}
	const auto current = EditorCommandContext::Capture(selection, selection.activeDocumentId, selection.revision);
	return commands.CanExecute(commandId, captured->second, current);
}

Result<void> ProjectBrowserPanel::ExecuteMenuCommand(const String& key, const String& commandId, const String& label,
                                                     const EditorSelectionContext& selection,
                                                     const EditorCommandAvailability& availability,
                                                     const EditorCommandAction& action) const
{
	const auto captured = contextMenuTargets.find(key);
	if (captured == contextMenuTargets.end())
	{
		return Result<void>::Failure("The Project context menu target is no longer available");
	}
	EditorCommands commands;
	auto registered = commands.Register({commandId, label, {}, availability, action});
	if (!registered)
	{
		return registered;
	}
	const auto current = EditorCommandContext::Capture(selection, selection.activeDocumentId, selection.revision);
	return commands.Execute(commandId, captured->second, current);
}

Result<void> ProjectBrowserPanel::Draw(ProjectWorkspace& workspace, ProjectBrowserModel& model,
                                       EditorSelectionContext& selection, EditorHierarchy& hierarchy, bool& open,
                                       bool readOnly, String& diagnostic, const ProjectBrowserActions& actions)
{
	if (!open)
	{
		return {};
	}
	const bool visible = ImGui::Begin("Project", &open);
	Icons::DrawPanelContextMenu("ProjectPanelActions", &open);
	if (!visible)
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
	if (selection.MissingCount())
	{
		ImGui::TextDisabled("%zu selected asset(s) no longer exist", selection.MissingCount());
	}
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	auto queueDelete = [&] { RequestDeleteSelected(selection); };
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
	const auto reimportPlan = workspace.Database()
	                              ? PlanProjectSourceReimport(*workspace.Database(), selection.entries, readOnly)
	                              : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
	ImGui::BeginDisabled(!actions.reimportAssets || !reimportPlan);
	if (Icons::Button("Reimport Selected"))
	{
		auto reimported = actions.reimportAssets(reimportPlan.value);
		diagnostic = reimported ? String{} : reimported.error;
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
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
	    ImGui::IsKeyPressed(ImGuiKey_F2))
	{
		const auto captured = EditorCommandContext::Capture(selection, selection.activeDocumentId, selection.revision);
		const auto current = EditorCommandContext::Capture(selection, selection.activeDocumentId, selection.revision);
		EditorCommands commands;
		auto registered =
		    commands.Register({"project.rename", "Rename", "F2",
		                       [readOnly, &actions](const EditorCommandContext& context)
		                       {
			                       const auto* primary = context.selection.PrimaryEntry();
			                       return !readOnly && actions.renameAsset && context.selection.entries.size() == 1 &&
			                                      primary && primary->kind == EditorSelectionKind::Asset &&
			                                      !primary->IsMissing() &&
			                                      primary->assetRole == EditorAssetSelectionRole::Source
			                                  ? Result<void>{}
			                                  : Result<void>::Failure("Rename requires one writable source asset");
		                       },
		                       [this](const EditorCommandContext&)
		                       {
			                       renameRequested = true;
			                       return Result<void>{};
		                       }});
		if (registered)
		{
			auto renamed = commands.Execute("project.rename", captured, current);
			if (!renamed && !selection.entries.empty())
			{
				diagnostic = renamed.error;
			}
		}
		else
		{
			diagnostic = registered.error;
		}
	}
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
		const ImRect rootFolderRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		DrawProjectMoveDropTarget(rootFolderRect, workspace, model, selection, {}, readOnly, diagnostic, actions);
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
			const ImRect folderRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			DrawProjectAssetDragSource(selection, AssetSelection(folder), folder.name);
			DrawProjectMoveDropTarget(folderRect, workspace, model, selection, folder.path.relative, readOnly,
			                          diagnostic, actions);
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
	Vector<EditorSelectionEntry> visibleRows;
	if (search.empty())
	{
		for (const auto& folder : model.ChildFolders(currentFolder))
		{
			visibleRows.push_back(AssetSelection(folder));
		}
	}
	const auto visibleAssets = model.ChildAssets(currentFolder, search);
	for (const auto& asset : visibleAssets)
	{
		visibleRows.push_back(AssetSelection(asset));
		if (expandedAssets[asset.guid.ToString()] || !search.empty())
		{
			for (const auto& object : asset.VisibleChildren())
			{
				if (!object.isMainObject && object.visibleInProject)
				{
					visibleRows.push_back(AssetSelection(asset, object.localFileId, object.kind));
				}
			}
		}
	}
	auto selectAsset = [&](const EditorSelectionEntry& entry)
	{
		const bool accepted =
		    selection.SelectClicked(entry, visibleRows, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
		if (accepted)
		{
			const auto* primary = selection.PrimaryEntry();
			if (primary && primary->kind == EditorSelectionKind::Asset)
			{
				model.SelectAsset(primary->assetPath);
			}
			hierarchy.ClearSelection();
		}
		return accepted;
	};
	if (search.empty())
	{
		for (const ProjectBrowserEntry& folder : model.ChildFolders(currentFolder))
		{
			ImGui::PushID(folder.path.relative.c_str());
			const bool folderSelected = Icons::Selectable(Icons::Icon::Folder, folder.name.c_str());
			const ImRect folderRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			if (folderSelected)
			{
				selectFolder(folder.path.relative);
			}
			DrawProjectAssetDragSource(selection, AssetSelection(folder), folder.name);
			DrawProjectMoveDropTarget(folderRect, workspace, model, selection, folder.path.relative, readOnly,
			                          diagnostic, actions);
			bool contextSelectionAccepted = true;
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				if (!selection.Contains(AssetSelection(folder)))
				{
					contextSelectionAccepted = selection.SelectAsset(folder.path);
					if (contextSelectionAccepted)
					{
						model.SelectAsset(folder.path);
						hierarchy.ClearSelection();
					}
				}
				if (contextSelectionAccepted)
				{
					CaptureMenuContext("folder:" + folder.path.relative, selection);
				}
			}
			if (contextSelectionAccepted && ImGui::BeginPopupContextItem("FolderActions"))
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
				const String commandKey = "folder:" + folder.path.relative;
				const auto canRename = CanExecuteMenuCommand(
				    commandKey, "project.rename", "Rename", selection,
				    [&](const EditorCommandContext& context)
				    {
					    const auto* primary = context.selection.PrimaryEntry();
					    return actions.renameAsset && !readOnly && context.selection.entries.size() == 1 && primary &&
					                   primary->kind == EditorSelectionKind::Asset &&
					                   primary->assetPath == folder.path &&
					                   primary->assetRole == EditorAssetSelectionRole::Source
					               ? Result<void>{}
					               : Result<void>::Failure("The folder selection changed or is read-only");
				    });
				if (Icons::MenuItem("Rename Selected", nullptr, false, static_cast<bool>(canRename)))
				{
					auto renamed = ExecuteMenuCommand(
					    commandKey, "project.rename", "Rename", selection,
					    [&](const EditorCommandContext& context)
					    {
						    const auto* primary = context.selection.PrimaryEntry();
						    return actions.renameAsset && !readOnly && context.selection.entries.size() == 1 &&
						                   primary && primary->kind == EditorSelectionKind::Asset &&
						                   primary->assetPath == folder.path &&
						                   primary->assetRole == EditorAssetSelectionRole::Source
						               ? Result<void>{}
						               : Result<void>::Failure("The folder selection changed or is read-only");
					    },
					    [&](const EditorCommandContext&)
					    {
						    model.SelectAsset(folder.path);
						    renameRequested = true;
						    return Result<void>{};
					    });
					diagnostic = renamed ? String{} : renamed.error;
				}
				const auto canDelete = CanExecuteMenuCommand(
				    commandKey, "project.delete", "Delete", selection,
				    [&](const EditorCommandContext& context)
				    {
					    auto plan =
					        workspace.Database()
					            ? PlanProjectSourceDelete(*workspace.Database(), context.selection.entries, readOnly)
					            : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
					    return actions.deleteAssets && plan
					               ? Result<void>{}
					               : Result<void>::Failure(plan ? "Project Delete is unavailable" : plan.error);
				    });
				if (Icons::MenuItem("Delete Selected", nullptr, false, static_cast<bool>(canDelete)))
				{
					auto queued = ExecuteMenuCommand(
					    commandKey, "project.delete", "Delete", selection,
					    [&](const EditorCommandContext& context)
					    {
						    auto plan =
						        workspace.Database()
						            ? PlanProjectSourceDelete(*workspace.Database(), context.selection.entries,
						                                      readOnly)
						            : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
						    return actions.deleteAssets && plan
						               ? Result<void>{}
						               : Result<void>::Failure(plan ? "Project Delete is unavailable" : plan.error);
					    },
					    [&](const EditorCommandContext&)
					    {
						    queueDelete();
						    return Result<void>{};
					    });
					diagnostic = queued ? String{} : queued.error;
				}
				const auto contextReimportPlan =
				    workspace.Database() ? PlanProjectSourceReimport(*workspace.Database(), selection.entries, readOnly)
				                         : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
				if (Icons::MenuItem("Reimport Selected", nullptr, false,
				                    static_cast<bool>(actions.reimportAssets) &&
				                        static_cast<bool>(contextReimportPlan)))
				{
					auto queued = ExecuteMenuCommand(
					    commandKey, "project.reimport", "Reimport", selection,
					    [&](const EditorCommandContext& context)
					    {
						    auto plan =
						        workspace.Database()
						            ? PlanProjectSourceReimport(*workspace.Database(), context.selection.entries,
						                                        readOnly)
						            : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
						    return actions.reimportAssets && plan
						               ? Result<void>{}
						               : Result<void>::Failure(plan ? "Project Reimport is unavailable" : plan.error);
					    },
					    [&](const EditorCommandContext& context)
					    {
						    auto plan =
						        PlanProjectSourceReimport(*workspace.Database(), context.selection.entries, readOnly);
						    return plan ? actions.reimportAssets(plan.value) : Result<void>::Failure(plan.error);
					    });
					diagnostic = queued ? String{} : queued.error;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
	}
	for (const ProjectBrowserEntry& asset : visibleAssets)
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
		const auto assetEntry = AssetSelection(asset);
		const bool selected = selection.Contains(assetEntry);
		const String label = search.empty() ? asset.name : asset.path.relative;
		if (Icons::Selectable(Icons::ForAsset(false, asset.kind, asset.importer, asset.path.relative), label.c_str(),
		                      selected, ImGuiSelectableFlags_AllowDoubleClick))
		{
			const bool accepted = selectAsset(assetEntry);
			if (accepted)
			{
				model.SelectAsset(asset.path);
			}
			if (accepted && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && actions.openAsset)
			{
				EditorCommands commands;
				commands.Register(
				    {"project.open",
				     "Open",
				     {},
				     [assetEntry](const EditorCommandContext& context)
				     {
					     const auto* primary = context.selection.PrimaryEntry();
					     return primary && primary->SameIdentity(assetEntry) && !primary->IsMissing()
					                ? Result<void>{}
					                : Result<void>::Failure("The Project selection changed before opening the asset");
				     },
				     [&actions, &asset](const EditorCommandContext&) { return actions.openAsset(asset); }});
				const auto commandContext =
				    EditorCommandContext::Capture(selection, selection.activeDocumentId, selection.revision);
				auto opened = commands.Execute("project.open", commandContext, commandContext);
				diagnostic = opened ? String{} : opened.error;
			}
		}
		DrawProjectAssetDragSource(selection, assetEntry, asset.name);
		bool contextSelectionAccepted = true;
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			if (!selection.Contains(assetEntry) && selection.SelectEntries({assetEntry}, 0, assetEntry))
			{
				model.SelectAsset(asset.path);
				hierarchy.ClearSelection();
			}
			else if (!selection.Contains(assetEntry))
			{
				contextSelectionAccepted = false;
			}
			if (contextSelectionAccepted)
			{
				CaptureMenuContext("asset:" + asset.guid.ToString(), selection);
			}
		}
		if (contextSelectionAccepted && ImGui::BeginPopupContextItem("AssetActions"))
		{
			const String commandKey = "asset:" + asset.guid.ToString();
			const auto canOpen = CanExecuteMenuCommand(
			    commandKey, "project.open", "Open", selection,
			    [&](const EditorCommandContext& context)
			    {
				    if (!actions.openAsset || context.selection.entries.empty())
				    {
					    return Result<void>::Failure("Select an asset that can be opened");
				    }
				    if (std::none_of(context.selection.entries.begin(), context.selection.entries.end(),
				                     [&](const auto& entry) { return entry.SameIdentity(assetEntry); }))
				    {
					    return Result<void>::Failure("The asset selection changed");
				    }
				    return Result<void>{};
			    });
			if (Icons::MenuItem("Open", nullptr, false, static_cast<bool>(canOpen)))
			{
				auto opened = ExecuteMenuCommand(
				    commandKey, "project.open", "Open", selection,
				    [&](const EditorCommandContext& context)
				    {
					    return !actions.openAsset || context.selection.entries.empty()
					               ? Result<void>::Failure("Select an asset that can be opened")
					           : std::none_of(context.selection.entries.begin(), context.selection.entries.end(),
					                          [&](const auto& entry) { return entry.SameIdentity(assetEntry); })
					               ? Result<void>::Failure("The asset selection changed")
					               : Result<void>{};
				    },
				    [&](const EditorCommandContext&)
				    {
					    model.SelectAsset(asset.path);
					    hierarchy.ClearSelection();
					    return actions.openAsset(asset);
				    });
				diagnostic = opened ? String{} : opened.error;
			}
			const auto canRename = CanExecuteMenuCommand(
			    commandKey, "project.rename", "Rename", selection,
			    [&](const EditorCommandContext& context)
			    {
				    const auto* primary = context.selection.PrimaryEntry();
				    return actions.renameAsset && !readOnly && context.selection.entries.size() == 1 && primary &&
				                   primary->kind == EditorSelectionKind::Asset && !primary->IsMissing() &&
				                   primary->assetRole == EditorAssetSelectionRole::Source &&
				                   primary->assetPath == asset.path
				               ? Result<void>{}
				               : Result<void>::Failure("Select a writable source asset to rename");
			    });
			if (Icons::MenuItem("Rename Selected", nullptr, false, static_cast<bool>(canRename)))
			{
				auto renamed = ExecuteMenuCommand(
				    commandKey, "project.rename", "Rename", selection,
				    [&](const EditorCommandContext& context)
				    {
					    const auto* primary = context.selection.PrimaryEntry();
					    return actions.renameAsset && !readOnly && context.selection.entries.size() == 1 && primary &&
					                   primary->kind == EditorSelectionKind::Asset && !primary->IsMissing() &&
					                   primary->assetRole == EditorAssetSelectionRole::Source &&
					                   primary->assetPath == asset.path
					               ? Result<void>{}
					               : Result<void>::Failure("Select a writable source asset to rename");
				    },
				    [&](const EditorCommandContext&)
				    {
					    renameRequested = true;
					    return Result<void>{};
				    });
				diagnostic = renamed ? String{} : renamed.error;
			}
			const auto canDelete = CanExecuteMenuCommand(
			    commandKey, "project.delete", "Delete", selection,
			    [&](const EditorCommandContext& context)
			    {
				    auto plan =
				        workspace.Database()
				            ? PlanProjectSourceDelete(*workspace.Database(), context.selection.entries, readOnly)
				            : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
				    return actions.deleteAssets && plan
				               ? Result<void>{}
				               : Result<void>::Failure(plan ? "Project Delete is unavailable" : plan.error);
			    });
			if (Icons::MenuItem("Delete Selected", nullptr, false, static_cast<bool>(canDelete)))
			{
				auto queued = ExecuteMenuCommand(
				    commandKey, "project.delete", "Delete", selection,
				    [&](const EditorCommandContext& context)
				    {
					    auto plan =
					        workspace.Database()
					            ? PlanProjectSourceDelete(*workspace.Database(), context.selection.entries, readOnly)
					            : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
					    return actions.deleteAssets && plan
					               ? Result<void>{}
					               : Result<void>::Failure(plan ? "Project Delete is unavailable" : plan.error);
				    },
				    [&](const EditorCommandContext&)
				    {
					    queueDelete();
					    return Result<void>{};
				    });
				diagnostic = queued ? String{} : queued.error;
			}
			const auto contextReimportPlan =
			    workspace.Database() ? PlanProjectSourceReimport(*workspace.Database(), selection.entries, readOnly)
			                         : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
			if (Icons::MenuItem("Reimport Selected", nullptr, false,
			                    static_cast<bool>(actions.reimportAssets) && static_cast<bool>(contextReimportPlan)))
			{
				auto queued = ExecuteMenuCommand(
				    commandKey, "project.reimport", "Reimport", selection,
				    [&](const EditorCommandContext& context)
				    {
					    auto plan =
					        workspace.Database()
					            ? PlanProjectSourceReimport(*workspace.Database(), context.selection.entries, readOnly)
					            : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
					    return actions.reimportAssets && plan
					               ? Result<void>{}
					               : Result<void>::Failure(plan ? "Project Reimport is unavailable" : plan.error);
				    },
				    [&](const EditorCommandContext& context)
				    {
					    auto plan =
					        PlanProjectSourceReimport(*workspace.Database(), context.selection.entries, readOnly);
					    return plan ? actions.reimportAssets(plan.value) : Result<void>::Failure(plan.error);
				    });
				diagnostic = queued ? String{} : queued.error;
			}
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
				const auto objectEntry = AssetSelection(asset, object.localFileId, object.kind);
				if (Icons::Selectable(
				        Icons::ForAsset(false,
				                        resourceKind ? std::optional<ResourceKind>(resourceKind.value) : std::nullopt,
				                        {}, {}),
				        objectLabel.c_str(),
				        selection.Contains(AssetSelection(asset, object.localFileId, object.kind))))
				{
					if (selectAsset(AssetSelection(asset, object.localFileId, object.kind)))
					{
					}
				}
				if (ImGui::BeginDragDropSource())
				{
					const auto drag = BeginEditorDragSession(selection, objectEntry);
					if (drag)
					{
						ImGui::SetDragDropPayload(editorDragSessionPayloadType, &drag.value, sizeof(drag.value));
					}
					ImGui::TextUnformatted(objectLabel.c_str());
					ImGui::EndDragDropSource();
				}
				bool contextObjectSelectionAccepted = true;
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selection.Contains(objectEntry))
				{
					contextObjectSelectionAccepted = selection.SelectEntries({objectEntry}, 0, objectEntry);
					if (contextObjectSelectionAccepted)
					{
						model.SelectAsset(asset.path);
						hierarchy.ClearSelection();
					}
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				{
					if (contextObjectSelectionAccepted)
					{
						CaptureMenuContext("object:" + asset.guid.ToString() + ":" + localId, selection);
					}
				}
				if (contextObjectSelectionAccepted && ImGui::BeginPopupContextItem("ImportedObjectActions"))
				{
					const String commandKey = "object:" + asset.guid.ToString() + ":" + localId;
					const auto canSelectSource = CanExecuteMenuCommand(
					    commandKey, "project.select-source", "Select Source", selection,
					    [&](const EditorCommandContext& context)
					    {
						    const auto* primary = context.selection.PrimaryEntry();
						    return primary && primary->kind == EditorSelectionKind::Asset &&
						                   primary->assetRole == EditorAssetSelectionRole::ImportedObject &&
						                   primary->assetGuid == asset.guid
						               ? Result<void>{}
						               : Result<void>::Failure("The imported object selection changed");
					    });
					if (Icons::MenuItem("Select Source", nullptr, false, static_cast<bool>(canSelectSource)))
					{
						auto selectedSource = ExecuteMenuCommand(
						    commandKey, "project.select-source", "Select Source", selection,
						    [&](const EditorCommandContext& context)
						    {
							    const auto* primary = context.selection.PrimaryEntry();
							    return primary && primary->kind == EditorSelectionKind::Asset &&
							                   primary->assetRole == EditorAssetSelectionRole::ImportedObject &&
							                   primary->assetGuid == asset.guid
							               ? Result<void>{}
							               : Result<void>::Failure("The imported object selection changed");
						    },
						    [&](const EditorCommandContext&)
						    {
							    return selection.SelectEntries({assetEntry}, 0, assetEntry)
							               ? (model.SelectAsset(asset.path), hierarchy.ClearSelection(), Result<void>{})
							               : Result<void>::Failure("The source asset could not be selected");
						    });
						diagnostic = selectedSource ? String{} : selectedSource.error;
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			ImGui::Unindent();
		}
		ImGui::PopID();
	}
	const ImVec2 blankDropMin = ImGui::GetCursorScreenPos();
	const ImRect contentWork = ImGui::GetCurrentWindow()->WorkRect;
	DrawProjectMoveDropTarget(ImRect(blankDropMin, contentWork.Max), workspace, model, selection, currentFolder,
	                          readOnly, diagnostic, actions);
	const ImVec2 folderContentsMin = ImGui::GetWindowPos();
	const ImVec2 folderContentsSize = ImGui::GetWindowSize();
	DrawScenePrefabDropTarget(
	    {folderContentsMin, {folderContentsMin.x + folderContentsSize.x, folderContentsMin.y + folderContentsSize.y}},
	    currentFolder, readOnly, selection.revision, diagnostic, actions);
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		backgroundMenuFolder = currentFolder;
	}
	if (ImGui::BeginPopupContextWindow("ProjectBackgroundActions",
	                                   ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		const String targetFolder = backgroundMenuFolder;
		EditorSelectionContext emptySelection;
		emptySelection.activeDocumentId = selection.activeDocumentId;
		emptySelection.revision = selection.revision;
		const EditorCommandContext captured =
		    EditorCommandContext::Capture(emptySelection, selection.activeDocumentId, selection.revision);
		const EditorCommandContext current =
		    EditorCommandContext::Capture(emptySelection, selection.activeDocumentId, selection.revision);
		EditorCommands commands;
		commands.Register({"project.background.create-folder",
		                   "Create Folder",
		                   {},
		                   [readOnly, &actions](const EditorCommandContext&)
		                   {
			                   return !readOnly && actions.createFolder
			                              ? Result<void>{}
			                              : Result<void>::Failure("Project folder creation is unavailable");
		                   },
		                   [&, targetFolder](const EditorCommandContext&)
		                   {
			                   auto navigated = model.NavigateTo(targetFolder);
			                   if (!navigated)
			                   {
				                   return navigated;
			                   }
			                   auto created = actions.createFolder(String(folderName.data()));
			                   if (created)
			                   {
				                   folderName.fill(0);
			                   }
			                   return created;
		                   }});
		const auto canCreateFolder = commands.CanExecute("project.background.create-folder", captured, current);
		if (Icons::MenuItem("Create Folder", nullptr, false, static_cast<bool>(canCreateFolder)))
		{
			diagnostic = commands.Execute("project.background.create-folder", captured, current).error;
		}
		ImGui::BeginDisabled(readOnly || !actions.createAsset);
		if (Icons::BeginMenu("Create Asset"))
		{
			for (const auto& item : {std::tuple<const char*, ResourceKind, MaterialType>{"Scene", ResourceKind::Scene,
			                                                                             MaterialType::Unlit},
			                         {"Prefab", ResourceKind::Prefab, MaterialType::Unlit},
			                         {"Unlit Material", ResourceKind::Material, MaterialType::Unlit},
			                         {"PBR Material", ResourceKind::Material, MaterialType::PbrMetallicRoughness},
			                         {"Texture", ResourceKind::Texture, MaterialType::Unlit}})
			{
				if (Icons::MenuItem(std::get<0>(item)))
				{
					auto navigated = model.NavigateTo(targetFolder);
					if (!navigated)
					{
						diagnostic = navigated.error;
					}
					else
					{
						destinationPath.fill(0);
						auto created = actions.createAsset(std::get<1>(item), std::get<2>(item), String{});
						diagnostic = created ? String{} : created.error;
						if (created)
						{
							selection.Clear();
							hierarchy.ClearSelection();
						}
					}
				}
			}
			for (const ScriptTypeDescriptor* type : scriptableAssetTypes)
			{
				if (Icons::MenuItem(type->createAssetMenuName.c_str(), nullptr, false,
				                    static_cast<bool>(actions.createScriptableAsset)))
				{
					auto navigated = model.NavigateTo(targetFolder);
					if (!navigated)
					{
						diagnostic = navigated.error;
					}
					else
					{
						auto created = actions.createScriptableAsset
						                   ? actions.createScriptableAsset(*type, type->createAssetFileName)
						                   : Result<void>::Failure("ScriptableObject asset creation is unavailable");
						diagnostic = created ? String{} : created.error;
						if (created)
						{
							selection.Clear();
							hierarchy.ClearSelection();
						}
					}
				}
			}
			ImGui::EndMenu();
		}
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}
	ImGui::EndChild();
	if (deleteRequested)
	{
		ImGui::OpenPopup("Delete Project Assets");
	}
	if (ImGui::BeginPopupModal("Delete Project Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto plan = workspace.Database() ? PlanProjectSourceDelete(*workspace.Database(), deleteTargets, readOnly)
		                                 : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
		if (!plan)
		{
			ImGui::TextWrapped("%s", plan.error.c_str());
		}
		else
		{
			ImGui::Text("Delete %zu source asset(s)? This removes their metadata and imported outputs too.",
			            plan.value.size());
			for (const VirtualPath& path : plan.value)
			{
				ImGui::BulletText("%s", path.relative.c_str());
			}
		}
		if (!deleteDiagnostic.empty())
		{
			ImGui::TextWrapped("%s", deleteDiagnostic.c_str());
		}
		if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f)))
		{
			plan = workspace.Database() ? PlanProjectSourceDelete(*workspace.Database(), deleteTargets, readOnly)
			                            : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
			if (!plan)
			{
				deleteDiagnostic = plan.error;
			}
			else if (!actions.deleteAssets)
			{
				deleteDiagnostic = "Project Delete is unavailable";
			}
			else
			{
				auto deleted = actions.deleteAssets(plan.value);
				if (!deleted)
				{
					deleteDiagnostic = deleted.error;
				}
				else
				{
					auto refreshed = model.Refresh(workspace);
					if (!refreshed)
					{
						deleteDiagnostic = refreshed.error;
					}
					else
					{
						selection.Clear();
						hierarchy.ClearSelection();
						deleteTargets.clear();
						deleteDiagnostic.clear();
						deleteRequested = false;
						ImGui::CloseCurrentPopup();
					}
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
		{
			deleteRequested = false;
			deleteTargets.clear();
			deleteDiagnostic.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
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
