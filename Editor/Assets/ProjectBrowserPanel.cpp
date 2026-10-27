#include <Ui/EditorIcons.hpp>
#include <Serialization/AssetReference.hpp>
#include <Assets/ProjectBrowserPanel.hpp>
#include <Assets/TextureImporter.hpp>
#include <Workspace/EditorDragSession.hpp>

#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cctype>
#include <functional>
#include <tuple>
#include <nlohmann/json.hpp>

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
                                const String& label, const EditorHierarchy::InputAction& inputAction)
{
	if (ImGui::BeginDragDropSource())
	{
		bool includeSelection = selection.Contains(entry) || ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
		if (inputAction)
		{
			UInt32 state = (1u << 26) | (selection.Contains(entry) ? 1u : 0u);
			auto managedDecision = inputAction(state);
			if (managedDecision)
			{
				includeSelection = managedDecision.value == 1;
			}
		}
		const auto drag = BeginEditorDragSession(selection, entry, includeSelection);
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

ProjectBrowserPanel::~ProjectBrowserPanel()
{
	ClearTextureThumbnails();
}

void ProjectBrowserPanel::ClearTextureThumbnails()
{
	if (thumbnailBridge)
	{
		for (const auto& [key, thumbnail] : textureThumbnails)
		{
			(void)key;
			if (thumbnail.id != ImTextureID_Invalid)
			{
				thumbnailBridge->RetireTexture(thumbnail.id);
			}
		}
	}
	textureThumbnails.clear();
	thumbnailBridge = nullptr;
	thumbnailProjectRoot.clear();
	thumbnailDatabaseRevision = 0;
}

ImTextureID ProjectBrowserPanel::ResolveTextureThumbnail(ProjectWorkspace& workspace, const ProjectBrowserEntry& asset,
                                                         UInt32& width, UInt32& height)
{
	width = 0;
	height = 0;
	if (asset.folder || asset.kind != ResourceKind::Texture || !workspace.Database())
	{
		return ImTextureID_Invalid;
	}
	ImGuiBridge* currentBridge = ImGuiBridge::Current();
	if (!currentBridge)
	{
		return ImTextureID_Invalid;
	}
	const String projectRoot = PathToUrsaString(workspace.ProjectRoot().lexically_normal());
	const UInt64 databaseRevision = workspace.Database()->Revision();
	if ((thumbnailBridge && thumbnailBridge != currentBridge) || thumbnailProjectRoot != projectRoot ||
	    thumbnailDatabaseRevision != databaseRevision)
	{
		ClearTextureThumbnails();
	}
	thumbnailBridge = currentBridge;
	thumbnailProjectRoot = projectRoot;
	thumbnailDatabaseRevision = databaseRevision;
	const String key = asset.guid.ToString();
	TextureThumbnail& thumbnail = textureThumbnails[key];
	if (!thumbnail.attempted)
	{
		thumbnail.attempted = true;
		TextureImportSettings settings;
		settings.generateMipmaps = false;
		settings.maxDimension = 256;
		auto decoded = TextureImporter::DecodeCandidate(workspace.Database()->Filesystem(), asset.path, settings);
		if (decoded)
		{
			auto uploaded = UploadTexturePreview(currentBridge->Device(), *currentBridge, decoded.value, thumbnail.id,
			                                     thumbnail.texture, thumbnail.sampler);
			if (uploaded)
			{
				thumbnail.width = decoded.value->GetWidth();
				thumbnail.height = decoded.value->GetHeight();
			}
			else
			{
				thumbnail.id = ImTextureID_Invalid;
			}
		}
	}
	width = thumbnail.width;
	height = thumbnail.height;
	return thumbnail.id;
}

void ProjectBrowserPanel::Reset()
{
	ClearTextureThumbnails();
	expandedAssets.clear();
	favoriteSearches.clear();
	favoriteAssetPaths.clear();
	assetClipboard.clear();
	assetClipboardProject.clear();
	assetClipboardCut = false;
	settingsFiles = nullptr;
	favoritesSettingsPath = {};
	favoritesSettingsProject.clear();
	searchText.fill(0);
	columnFocusRequest = ColumnFocusRequest::None;
	contentRowKeyboardFocused = false;
	destinationPath.fill(0);
	folderName.fill(0);
	createFolderRequested = false;
	renameRequested = false;
	renameEditing = false;
	renameFocusRequested = false;
	renameSourcePath.clear();
	renameName.fill(0);
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
                                       bool readOnly, String& diagnostic, const ProjectBrowserActions& actions,
                                       const EditorHierarchy::InputAction& inputAction)
{
	Vector<ProjectBrowserFileDrop> externalDrops = actions.takeExternalFileDrops ? actions.takeExternalFileDrops()
	                                                                            : Vector<ProjectBrowserFileDrop>{};
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
	settingsFiles = &workspace.Index().Filesystem();
	UInt64 projectHash = 14695981039346656037ull;
	const String projectRoot = PathToUrsaString(workspace.ProjectRoot().lexically_normal());
	for (const unsigned char character : projectRoot)
	{
		projectHash ^= character;
		projectHash *= 1099511628211ull;
	}
	const String projectKey = ToUrsaString(std::to_string(projectHash));
	if (favoritesSettingsProject != projectKey)
	{
		favoritesSettingsProject = projectKey;
		assetClipboard.clear();
		assetClipboardProject = projectKey;
		assetClipboardCut = false;
		favoriteSearches.clear();
		favoriteAssetPaths.clear();
		twoColumnLayout = true;
		favoritesSettingsPath = VirtualPath::Parse("user:Settings/ProjectBrowserFavorites/" + projectKey + ".json").value;
		if (settingsFiles->IsMounted("user"))
		{
			auto exists = settingsFiles->Exists(favoritesSettingsPath);
			if (exists && exists.value)
			{
				auto contents = settingsFiles->ReadText(favoritesSettingsPath);
				try
				{
					if (!contents)
					{
						throw std::runtime_error(contents.error.c_str());
					}
					const auto document = nlohmann::json::parse(ToStdString(contents.value));
					if (document.contains("twoColumnLayout"))
					{
						twoColumnLayout = document.at("twoColumnLayout").get<bool>();
					}
					for (const auto& query : document.at("searches"))
					{
						const String value = ToUrsaString(query.get<std::string>());
						if (!value.empty() && std::find(favoriteSearches.begin(), favoriteSearches.end(), value) == favoriteSearches.end())
						{
							favoriteSearches.push_back(value);
						}
					}
					if (document.contains("assets"))
					{
						for (const auto& asset : document.at("assets"))
						{
							const String path = ToUrsaString(asset.get<std::string>());
						if (!path.empty() &&
						    std::find(favoriteAssetPaths.begin(), favoriteAssetPaths.end(), path) == favoriteAssetPaths.end())
							{
								favoriteAssetPaths.push_back(path);
							}
						}
					}
				}
				catch (const std::exception& error)
				{
					diagnostic = "Could not load Project favorites: " + ToUrsaString(error.what());
				}
			}
		}
	}
	auto saveProjectBrowserSettings = [&]()
	{
		if (!settingsFiles || !settingsFiles->IsMounted("user"))
		{
			diagnostic = "Project browser settings require the user settings mount.";
			return;
		}
		std::vector<std::string> searches;
		std::vector<std::string> assets;
		searches.reserve(favoriteSearches.size());
		assets.reserve(favoriteAssetPaths.size());
		for (const String& favorite : favoriteSearches)
		{
			searches.push_back(ToStdString(favorite));
		}
		for (const String& favorite : favoriteAssetPaths)
		{
			assets.push_back(ToStdString(favorite));
		}
		const std::string serialized = nlohmann::json{{"searches", std::move(searches)}, {"assets", std::move(assets)},
		                                             {"twoColumnLayout", twoColumnLayout}}
		                                   .dump(2);
		const auto* begin = reinterpret_cast<const UInt8*>(serialized.data());
		Vector<UInt8> bytes(begin, begin + serialized.size());
		auto saved = settingsFiles->WriteAtomic(favoritesSettingsPath, bytes);
		diagnostic = saved ? String{} : "Could not save Project browser settings: " + saved.error;
	};
	auto isFavoriteAsset = [&](const String& path)
	{
		return std::find(favoriteAssetPaths.begin(), favoriteAssetPaths.end(), path) != favoriteAssetPaths.end();
	};
	auto toggleFavoriteAsset = [&](const String& path)
	{
		const auto favorite = std::find(favoriteAssetPaths.begin(), favoriteAssetPaths.end(), path);
		if (favorite == favoriteAssetPaths.end())
		{
			favoriteAssetPaths.push_back(path);
		}
		else
		{
			favoriteAssetPaths.erase(favorite);
		}
		saveProjectBrowserSettings();
	};
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
	if (selection.MissingCount())
	{
		ImGui::TextDisabled("%zu selected asset(s) no longer exist", selection.MissingCount());
	}
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	auto queueDelete = [&] { RequestDeleteSelected(selection); };
	auto duplicateEntries = [&](const Vector<EditorSelectionEntry>& entries) -> Result<void>
	{
		if (readOnly || !workspace.Database() || !actions.duplicateAssets || entries.empty())
		{
			return Result<void>::Failure("Duplicate requires writable Project source assets");
		}
		Vector<VirtualPath> sources;
		Set<String> seen;
		for (const EditorSelectionEntry& entry : entries)
		{
			if (entry.kind != EditorSelectionKind::Asset || entry.assetRole != EditorAssetSelectionRole::Source ||
			    entry.assetLocalFileId || !entry.assetGuid || entry.IsMissing())
			{
				return Result<void>::Failure("Duplicate accepts only project source assets and folders");
			}
			auto source = workspace.Database()->SourcePath(entry.assetGuid);
			if (!source || source.value != entry.assetPath || source.value.scheme != workspace.Database()->SourceScheme())
			{
				return Result<void>::Failure("A selected source changed or disappeared before duplication");
			}
			if (seen.insert(source.value.ToString()).second)
			{
				sources.push_back(source.value);
			}
		}
		auto duplicated = actions.duplicateAssets(sources);
		if (!duplicated)
		{
			return Result<void>::Failure(duplicated.error);
		}
		auto rebuilt = model.Refresh(workspace);
		if (!rebuilt)
		{
			return rebuilt;
		}
		Vector<EditorSelectionEntry> copies;
		for (const VirtualPath& path : duplicated.value)
		{
			const auto* asset = model.Find(path);
			if (asset)
			{
				copies.push_back(AssetSelection(*asset));
			}
		}
		if (!copies.empty())
		{
			model.SelectAsset(copies.front().assetPath);
			selection.SelectEntries(std::move(copies), 0);
		}
		model.ReconcileSelection(selection);
		return Result<void>{};
	};
	auto setAssetClipboard = [&](const Vector<EditorSelectionEntry>& entries, bool cut) -> Result<void>
	{
		Vector<VirtualPath> sources;
		if (entries.empty() || !workspace.Database())
		{
			return Result<void>::Failure("Project Copy and Cut require selected source assets");
		}
		Set<String> seen;
		for (const EditorSelectionEntry& entry : entries)
		{
			if (entry.kind != EditorSelectionKind::Asset || entry.assetRole != EditorAssetSelectionRole::Source ||
			    entry.assetLocalFileId || !entry.assetGuid || entry.IsMissing())
			{
				return Result<void>::Failure("Project Copy and Cut accept only source assets and folders");
			}
			auto source = workspace.Database()->SourcePath(entry.assetGuid);
			if (!source || source.value != entry.assetPath || source.value.scheme != workspace.Database()->SourceScheme())
			{
				return Result<void>::Failure("A selected source changed or disappeared before copying");
			}
			if (seen.insert(source.value.ToString()).second)
			{
				sources.push_back(source.value);
			}
		}
		for (Size index = 0; index < sources.size();)
		{
			bool nested = false;
			for (const VirtualPath& candidate : sources)
			{
				if (candidate == sources[index])
				{
					continue;
				}
				auto metadata = workspace.Database()->Metadata(candidate);
				if (metadata && metadata.value.folder &&
				    sources[index].relative.size() > candidate.relative.size() &&
				    sources[index].relative.compare(0, candidate.relative.size(), candidate.relative) == 0 &&
				    sources[index].relative[candidate.relative.size()] == '/')
				{
					nested = true;
					break;
				}
			}
			if (nested)
			{
				sources.erase(sources.begin() + index);
			}
			else
			{
				++index;
			}
		}
		assetClipboard = std::move(sources);
		assetClipboardProject = projectKey;
		assetClipboardCut = cut;
		return {};
	};
	auto pasteAssetClipboard = [&](const String& destinationFolder) -> Result<void>
	{
		if (assetClipboard.empty() || assetClipboardProject != projectKey)
		{
			return Result<void>::Failure("The Project clipboard is empty");
		}
		if (readOnly || !actions.pasteAssets)
		{
			return Result<void>::Failure("Project Paste is unavailable while the project is read-only");
		}
		auto pasted = actions.pasteAssets(assetClipboard, destinationFolder, assetClipboardCut);
		if (!pasted)
		{
			return Result<void>::Failure(pasted.error);
		}
		auto refreshed = model.Refresh(workspace);
		if (!refreshed)
		{
			return refreshed;
		}
		Vector<EditorSelectionEntry> pastedEntries;
		for (const VirtualPath& path : pasted.value)
		{
			if (const auto* asset = model.Find(path))
			{
				pastedEntries.push_back(AssetSelection(*asset));
			}
		}
		if (!pastedEntries.empty())
		{
			model.SelectAsset(pastedEntries.front().assetPath);
			selection.SelectEntries(std::move(pastedEntries), 0);
			hierarchy.ClearSelection();
		}
		model.ReconcileSelection(selection);
		if (assetClipboardCut)
		{
			assetClipboard.clear();
			assetClipboardCut = false;
		}
		return {};
	};
	auto canCopyCutSelection = [&]()
	{
		return !selection.entries.empty() &&
		       std::all_of(selection.entries.begin(), selection.entries.end(),
		                   [](const EditorSelectionEntry& entry)
	                   {
		                   return entry.kind == EditorSelectionKind::Asset &&
		                          entry.assetRole == EditorAssetSelectionRole::Source && !entry.assetLocalFileId &&
		                          entry.assetGuid && !entry.IsMissing();
	                   });
	};
	bool pasteRequestedFromMenu = false;
	String pasteMenuDestination;
	auto navigateHistory = [&](bool backward)
	{
		if (!selection.Clear())
		{
			return;
		}
		auto navigated = backward ? model.NavigateBack() : model.NavigateForward();
		diagnostic = navigated ? String{} : navigated.error;
		if (navigated)
		{
			model.ClearAssetSelection();
			hierarchy.ClearSelection();
		}
	};
	auto requestCreateFolder = [&]
	{
		folderName.fill(0);
		std::copy_n("New Folder", 11, folderName.data());
		createFolderRequested = true;
	};
	UInt32 projectInputCommand = 0;
	bool managedProjectInputHandled = false;
	if (inputAction)
	{
		UInt32 state = 1u << 25;
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			state |= 1u;
		}
		if (model.CanNavigateBack())
		{
			state |= 1u << 1;
		}
		if (model.CanNavigateForward())
		{
			state |= 1u << 2;
		}
		if (!readOnly && actions.createFolder)
		{
			state |= 1u << 3;
		}
		if (assetClipboardCut)
		{
			state |= 1u << 4;
		}
		if (selection.entries.size() == 1)
		{
			state |= 1u << 5;
		}
		auto command = inputAction(state);
		managedProjectInputHandled = static_cast<bool>(command);
		if (command)
		{
			projectInputCommand = command.value;
		}
	}
	if (!managedProjectInputHandled)
	{
		const ImGuiIO& input = ImGui::GetIO();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !input.WantTextInput)
		{
			if (input.KeyAlt && !input.KeyCtrl && !input.KeyShift && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) &&
			    model.CanNavigateBack())
			{
				projectInputCommand = 1;
			}
			else if (input.KeyAlt && !input.KeyCtrl && !input.KeyShift &&
			         ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && model.CanNavigateForward())
			{
				projectInputCommand = 2;
			}
			else if ((input.KeyCtrl || input.KeySuper) && input.KeyShift && !input.KeyAlt &&
			         ImGui::IsKeyPressed(ImGuiKey_N, false) && !readOnly && actions.createFolder)
			{
				projectInputCommand = 3;
			}
			else if (assetClipboardCut && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			{
				projectInputCommand = 4;
			}
		}
	}
	if (projectInputCommand == 1)
	{
		navigateHistory(true);
	}
	else if (projectInputCommand == 2)
	{
		navigateHistory(false);
	}
	else if (projectInputCommand == 3)
	{
		requestCreateFolder();
	}
	else if (projectInputCommand == 4)
	{
		assetClipboard.clear();
		assetClipboardCut = false;
	}
	ImGui::BeginDisabled(!model.CanNavigateBack());
	if (ImGui::ArrowButton("ProjectNavigateBack", ImGuiDir_Left))
	{
		navigateHistory(true);
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Back");
	}
	Icons::SameLine();
	ImGui::BeginDisabled(!model.CanNavigateForward());
	if (ImGui::ArrowButton("ProjectNavigateForward", ImGuiDir_Right))
	{
		navigateHistory(false);
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Forward");
	}
	Icons::SameLine();
	if (ImGui::SmallButton("⋮"))
	{
		ImGui::OpenPopup("ProjectLayoutMenu");
	}
	if (ImGui::BeginPopup("ProjectLayoutMenu"))
	{
		if (ImGui::MenuItem("One Column Layout", nullptr, !twoColumnLayout))
		{
			twoColumnLayout = false;
			saveProjectBrowserSettings();
		}
		if (ImGui::MenuItem("Two Column Layout", nullptr, twoColumnLayout))
		{
			twoColumnLayout = true;
			saveProjectBrowserSettings();
		}
		ImGui::EndPopup();
	}
	if (!twoColumnLayout && (!favoriteSearches.empty() || !favoriteAssetPaths.empty()))
	{
		Icons::SameLine();
		if (ImGui::SmallButton("Favorites"))
		{
			ImGui::OpenPopup("ProjectFavoritesMenu");
		}
		if (ImGui::BeginPopup("ProjectFavoritesMenu"))
		{
			for (const String& query : favoriteSearches)
			{
				if (ImGui::MenuItem(query.c_str()))
				{
					std::fill(searchText.begin(), searchText.end(), '\0');
					std::copy_n(query.data(), std::min(query.size(), searchText.size() - 1), searchText.data());
					selection.Clear();
					model.NavigateTo({});
					model.ClearAssetSelection();
					hierarchy.ClearSelection();
				}
			}
			if (!favoriteSearches.empty() && !favoriteAssetPaths.empty())
			{
				ImGui::Separator();
			}
			for (const String& path : favoriteAssetPaths)
			{
				const ProjectBrowserEntry* asset = model.Find(path);
				const Size separator = path.rfind('/');
				const String label = asset ? asset->name : separator == String::npos ? path : path.substr(separator + 1);
				ImGui::BeginDisabled(!asset);
				const bool selected = ImGui::MenuItem(label.c_str());
				ImGui::EndDisabled();
				if (selected && asset)
				{
					std::fill(searchText.begin(), searchText.end(), '\0');
					const String destination = asset->folder
					                               ? asset->path.relative
					                               : asset->path.relative.substr(0, asset->path.relative.rfind('/') == String::npos
				                                                                             ? 0
				                                                                             : asset->path.relative.rfind('/'));
					auto navigated = model.NavigateTo(destination);
					diagnostic = navigated ? String{} : navigated.error;
					if (navigated)
					{
						selection.Clear();
						if (!asset->folder)
						{
							const auto entry = AssetSelection(*asset);
						selection.SelectEntries({entry}, 0, entry);
						model.SelectAsset(asset->path);
						}
						else
						{
							model.ClearAssetSelection();
						}
						hierarchy.ClearSelection();
					}
				}
			}
			ImGui::EndPopup();
		}
	}
	Icons::SameLine();
	ImGui::BeginDisabled(readOnly);
	if (Icons::Button("Create", Icons::Icon::Add, "Create asset or folder"))
	{
		ImGui::OpenPopup("ProjectCreateMenu");
	}
	ImGui::EndDisabled();
	Icons::SameLine();
	if (actions.refresh && Icons::Button("Refresh", Icons::Icon::Refresh, "Refresh assets (Ctrl+R)"))
	{
		auto refreshed = actions.refresh();
		diagnostic = refreshed ? String{} : refreshed.error;
	}
	if (ImGui::BeginPopup("ProjectCreateMenu"))
	{
		if (Icons::MenuItem("Folder", nullptr, false, !readOnly && static_cast<bool>(actions.createFolder)))
		{
			requestCreateFolder();
		}
		ImGui::Separator();
		ImGui::BeginDisabled(readOnly || !actions.createAsset);
		for (const auto& item :
		     {std::tuple<const char*, ResourceKind, MaterialType>{"Scene", ResourceKind::Scene, MaterialType::Unlit},
		      {"Prefab", ResourceKind::Prefab, MaterialType::Unlit},
		      {"Unlit Material", ResourceKind::Material, MaterialType::Unlit},
		      {"PBR Material", ResourceKind::Material, MaterialType::PbrMetallicRoughness},
		      {"Texture", ResourceKind::Texture, MaterialType::Unlit}})
		{
			if (Icons::MenuItem(std::get<0>(item)))
			{
				auto created = actions.createAsset(std::get<1>(item), std::get<2>(item), String{});
				diagnostic = created ? String{} : created.error;
				if (created)
				{
					selection.Clear();
					hierarchy.ClearSelection();
				}
			}
		}
		ImGui::EndDisabled();
		for (const ScriptTypeDescriptor* type : scriptableAssetTypes)
		{
			if (Icons::MenuItem(type->createAssetMenuName.c_str(), nullptr, false,
			                    !readOnly && static_cast<bool>(actions.createScriptableAsset)))
			{
				auto created = actions.createScriptableAsset(*type, type->createAssetFileName);
				diagnostic = created ? String{} : created.error;
				if (created)
				{
					selection.Clear();
					hierarchy.ClearSelection();
				}
			}
		}
		ImGui::EndPopup();
	}
	if (createFolderRequested)
	{
		ImGui::OpenPopup("Create Project Folder");
		createFolderRequested = false;
	}
	if (ImGui::BeginPopupModal("Create Project Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		const String targetFolder =
		    model.CurrentFolder().empty() ? String("Assets") : String("Assets/") + model.CurrentFolder();
		ImGui::Text("Create folder in %s", targetFolder.c_str());
		ImGui::SetNextItemWidth(260.f);
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}
		const bool submit = ImGui::InputText("Name", folderName.data(), folderName.size(),
		                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		ImGui::BeginDisabled(readOnly || !actions.createFolder);
		const bool createClicked = Icons::Button("Create");
		if ((submit || createClicked) && actions.createFolder)
		{
			auto created = actions.createFolder(String(folderName.data()));
			diagnostic = created ? String{} : created.error;
			if (created)
			{
				folderName.fill(0);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndDisabled();
		Icons::SameLine();
		if (Icons::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (contentRowKeyboardFocused && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	    !ImGui::GetIO().WantTextInput &&
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
	if (contentRowKeyboardFocused && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	    !ImGui::GetIO().WantTextInput &&
	    ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && !selection.entries.empty())
	{
		auto duplicated = duplicateEntries(selection.entries);
		diagnostic = duplicated ? String{} : duplicated.error;
	}
	const ImGuiIO& assetClipboardInput = ImGui::GetIO();
	const bool projectClipboardFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	                                     !assetClipboardInput.WantTextInput &&
	                                     (assetClipboardInput.KeyCtrl || assetClipboardInput.KeySuper) &&
	                                     !assetClipboardInput.KeyAlt;
	if (projectClipboardFocused &&
	    (ImGui::IsKeyPressed(ImGuiKey_C, false) || ImGui::IsKeyPressed(ImGuiKey_X, false)))
	{
		auto copied = setAssetClipboard(selection.entries, ImGui::IsKeyPressed(ImGuiKey_X, false));
		diagnostic = copied ? String{} : copied.error;
	}
	if (projectClipboardFocused && ImGui::IsKeyPressed(ImGuiKey_V, false) &&
	    !assetClipboard.empty() && assetClipboardProject == projectKey)
	{
		auto pasted = pasteAssetClipboard(model.CurrentFolder());
		diagnostic = pasted ? String{} : pasted.error;
	}
	if (contentRowKeyboardFocused && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	    !ImGui::GetIO().WantTextInput &&
	    !selection.entries.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
	    std::all_of(selection.entries.begin(), selection.entries.end(),
	                [](const EditorSelectionEntry& entry)
	                {
		                return entry.kind == EditorSelectionKind::Asset && !entry.IsMissing() &&
		                       entry.assetRole == EditorAssetSelectionRole::Source && !entry.assetLocalFileId;
	                }))
	{
		if (ImGui::GetIO().KeyShift)
		{
			auto plan = workspace.Database() ? PlanProjectSourceDelete(*workspace.Database(), selection.entries, readOnly)
			                                : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
			if (!plan)
			{
				diagnostic = plan.error;
			}
			else if (!actions.deleteAssets)
			{
				diagnostic = "Project Delete is unavailable";
			}
			else
			{
				auto deleted = actions.deleteAssets(plan.value);
				diagnostic = deleted ? String{} : deleted.error;
				if (deleted)
				{
					auto refreshed = model.Refresh(workspace);
					diagnostic = refreshed ? String{} : refreshed.error;
					if (refreshed)
					{
						selection.Clear();
						hierarchy.ClearSelection();
					}
				}
			}
		}
		else
		{
			RequestDeleteSelected(selection);
		}
	}
	if (renameRequested)
	{
		renameRequested = false;
		const auto* primary = selection.PrimaryEntry();
		const ProjectBrowserEntry* asset =
		    primary && primary->kind == EditorSelectionKind::Asset && !primary->IsMissing() &&
		            primary->assetRole == EditorAssetSelectionRole::Source && !primary->assetLocalFileId
		        ? model.Find(primary->assetPath)
		        : nullptr;
		if (asset && actions.renameAsset && !readOnly)
		{
			renameSourcePath = asset->path.relative;
			String name = asset->name;
			if (!asset->folder)
			{
				const Size extension = name.rfind('.');
				if (extension != String::npos && extension > 0)
				{
					name.resize(extension);
				}
			}
			renameName.fill(0);
			std::copy_n(name.data(), std::min(name.size(), renameName.size() - 1), renameName.data());
			renameEditing = true;
			renameFocusRequested = true;
		}
		else
		{
			diagnostic = "Select one writable Project asset to rename";
		}
	}
	ImGui::Separator();
	const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
	const float projectFooterHeight = ImGui::GetFrameHeightWithSpacing();
	const float minimumTreeWidth = std::min(100.0f, availableWidth * 0.4f);
	const float minimumContentsWidth = std::min(160.0f, availableWidth * 0.5f);
	const float maximumTreeWidth =
	    std::max(minimumTreeWidth, availableWidth - ImGui::GetStyle().ItemSpacing.x - minimumContentsWidth);
	const String currentFolder = model.CurrentFolder();
	Vector<ImVec2> externalDropPositions;
	Vector<String> externalDropDestinations;
	Vector<bool> externalDropAccepted;
	externalDropPositions.reserve(externalDrops.size());
	externalDropDestinations.reserve(externalDrops.size());
	externalDropAccepted.reserve(externalDrops.size());
	const ImVec2 viewportPosition = ImGui::GetMainViewport()->Pos;
	const ImVec2 projectWindowPosition = ImGui::GetWindowPos();
	const ImVec2 projectWindowSize = ImGui::GetWindowSize();
	const ImRect projectWindowRect(projectWindowPosition, {projectWindowPosition.x + projectWindowSize.x,
	                                                       projectWindowPosition.y + projectWindowSize.y});
	for (const ProjectBrowserFileDrop& drop : externalDrops)
	{
		const ImVec2 position{viewportPosition.x + static_cast<Float32>(drop.x),
		                      viewportPosition.y + static_cast<Float32>(drop.y)};
		externalDropPositions.push_back(position);
		externalDropAccepted.push_back(projectWindowRect.Contains(position));
		externalDropDestinations.push_back(projectWindowRect.Contains(position) ? currentFolder : String{});
	}
	auto recordExternalDropFolder = [&](const ImRect& rect, const String& destination)
	{
		for (Size index = 0; index < externalDropPositions.size(); ++index)
		{
			if (externalDropAccepted[index] && rect.Contains(externalDropPositions[index]))
			{
				externalDropDestinations[index] = destination;
			}
		}
	};
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
	auto openFavoriteAsset = [&](const String& path)
	{
		const ProjectBrowserEntry* asset = model.Find(path);
		if (!asset)
		{
			diagnostic = "The favorited Project asset no longer exists: " + path;
			return;
		}
		if (asset->folder)
		{
			selectFolder(asset->path.relative);
			return;
		}
		String containingFolder = asset->path.relative;
		const Size separator = containingFolder.rfind('/');
		containingFolder = separator == String::npos ? String{} : containingFolder.substr(0, separator);
		std::fill(searchText.begin(), searchText.end(), '\0');
		auto navigated = model.NavigateTo(containingFolder);
		diagnostic = navigated ? String{} : navigated.error;
		if (navigated)
		{
			const auto entry = AssetSelection(*asset);
			selection.SelectEntries({entry}, 0, entry);
			model.SelectAsset(asset->path);
			hierarchy.ClearSelection();
		}
	};
	bool columnFocusMoved = false;
	if (!twoColumnLayout)
	{
		columnFocusRequest = ColumnFocusRequest::None;
	}
	if (twoColumnLayout)
	{
		if (columnFocusRequest == ColumnFocusRequest::FolderTree)
		{
			ImGui::SetNextWindowFocus();
			columnFocusRequest = ColumnFocusRequest::None;
			columnFocusMoved = true;
		}
		ImGui::SetNextWindowSizeConstraints({minimumTreeWidth, 0.0f}, {maximumTreeWidth, FLT_MAX});
		ImGui::BeginChild("ProjectFolderTree", ImVec2(190.0f, -projectFooterHeight),
		                  ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.f);
		const ImGuiIO& folderTreeInput = ImGui::GetIO();
		if (!columnFocusMoved && ImGui::IsWindowFocused() && !folderTreeInput.WantTextInput &&
		    !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
		{
			columnFocusRequest = ColumnFocusRequest::Contents;
		}
	const bool favoritesOpen = Icons::TreeNode("Favorites", ImGuiTreeNodeFlags_DefaultOpen |
		                                                                       ImGuiTreeNodeFlags_SpanAvailWidth,
	                                          Icons::Icon::Favorite, "Favorites", 16.f);
	const ImRect favoritesRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	if (ImGui::BeginDragDropTargetCustom(favoritesRect, ImGui::GetID("##favoriteAssetDropTarget")))
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
			        editorDragSessionPayloadType, ImGuiDragDropFlags_AcceptNoDrawDefaultRect |
			                                                                ImGuiDragDropFlags_AcceptBeforeDelivery))
			{
				auto decoded = DecodeEditorDragPayload(payload->DataType, payload->Data,
				                                      static_cast<Size>(payload->DataSize), selection.revision);
				if (!decoded)
				{
					if (payload->IsDelivery())
					{
						diagnostic = decoded.error;
					}
				}
				else
				{
					const bool projectFolders = !decoded.value.sources.empty() &&
					                            std::all_of(decoded.value.sources.begin(), decoded.value.sources.end(),
					                                        [&](const EditorDragSource& source)
					                                        {
						                                        const auto* asset = model.Find(source.identity.assetPath);
						                                        return source.identity.kind == EditorSelectionKind::Asset &&
						                                               !source.identity.IsMissing() &&
						                                               source.identity.assetRole ==
						                                                   EditorAssetSelectionRole::Source &&
						                                               !source.identity.assetLocalFileId && workspace.Database() &&
						                                               source.identity.assetPath.scheme ==
						                                                   workspace.Database()->SourceScheme() &&
						                                               asset && asset->folder;
					                                        });
					if (projectFolders)
					{
						ImGui::GetWindowDrawList()->AddRectFilled(
						    favoritesRect.Min, favoritesRect.Max,
						    ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.3f));
						if (payload->IsDelivery() && ClaimEditorDragDropDelivery(payload))
						{
							bool changedFavorites = false;
							for (const EditorDragSource& source : decoded.value.sources)
							{
								const String& path = source.identity.assetPath.relative;
								if (std::find(favoriteAssetPaths.begin(), favoriteAssetPaths.end(), path) ==
								    favoriteAssetPaths.end())
								{
									favoriteAssetPaths.push_back(path);
									changedFavorites = true;
								}
							}
							if (changedFavorites)
							{
								saveProjectBrowserSettings();
							}
							}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
	if (favoritesOpen)
	{
		if (favoriteSearches.empty() && favoriteAssetPaths.empty())
		{
			ImGui::TextDisabled("Drag Project folders here or save a search.");
		}
			for (Size index = 0; index < favoriteSearches.size();)
			{
				const String query = favoriteSearches[index];
				ImGui::PushID(static_cast<Int32>(index));
				const bool active = !std::strcmp(searchText.data(), query.c_str()) && currentFolder.empty();
				if (Icons::Selectable(Icons::Icon::Search, query.c_str(), active,
				                      ImGuiSelectableFlags_AllowDoubleClick))
				{
					std::fill(searchText.begin(), searchText.end(), '\0');
					std::copy_n(query.data(), std::min(query.size(), searchText.size() - 1), searchText.data());
					selectFolder({});
				}
				if (ImGui::BeginPopupContextItem("FavoriteActions"))
				{
					if (ImGui::MenuItem("Remove Favorite"))
					{
						favoriteSearches.erase(favoriteSearches.begin() + index);
						saveProjectBrowserSettings();
						ImGui::EndPopup();
						ImGui::PopID();
						continue;
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
				++index;
			}
			for (Size index = 0; index < favoriteAssetPaths.size();)
			{
				const String path = favoriteAssetPaths[index];
				const ProjectBrowserEntry* asset = model.Find(path);
				const Size separator = path.rfind('/');
				const String label = asset ? asset->name : separator == String::npos ? path : path.substr(separator + 1);
				const Icons::Icon icon = asset
				                             ? Icons::ForAsset(asset->folder, asset->kind, asset->importer,
				                                               asset->path.relative)
				                             : Icons::Icon::Folder;
				ImGui::PushID(static_cast<Int32>(favoriteSearches.size() + index));
				ImGui::BeginDisabled(!asset);
				const bool clicked = Icons::Selectable(icon, label.c_str(),
				                                       asset && selection.Contains(AssetSelection(*asset)));
				ImGui::EndDisabled();
				if (clicked)
				{
					openFavoriteAsset(path);
				}
				if (ImGui::BeginPopupContextItem("FavoriteAssetActions"))
				{
					if (ImGui::MenuItem("Remove Favorite"))
					{
						favoriteAssetPaths.erase(favoriteAssetPaths.begin() + index);
						saveProjectBrowserSettings();
						ImGui::EndPopup();
						ImGui::PopID();
						continue;
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
				++index;
			}
		ImGui::TreePop();
	}
	auto setFolderNodeSubtreeOpen = [&](const ProjectBrowserEntry& entry, bool expanded, auto&& self) -> void
	{
		ImGui::PushID(entry.path.relative.c_str());
		const ImGuiID nodeId = ImGui::GetID("##folder");
		ImGui::GetStateStorage()->SetInt(nodeId, expanded ? 1 : 0);
		ImGui::PushID(nodeId);
		for (const ProjectBrowserEntry& child : model.ChildFolders(entry.path.relative))
		{
			self(child, expanded, self);
		}
		ImGui::PopID();
		ImGui::PopID();
	};
	const bool assetsOpen = Icons::TreeNode("Assets",
	                                        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
	                                            (currentFolder.empty() ? ImGuiTreeNodeFlags_Selected : 0),
	                                        Icons::Icon::Folder, "Assets", 16.f);
	const bool recursivelyToggleAssets = ImGui::IsItemToggledOpen() && ImGui::GetIO().KeyAlt;
	if (assetsOpen)
	{
		if (ImGui::IsItemClicked())
		{
			selectFolder({});
		}
		const ImRect rootFolderRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		recordExternalDropFolder(rootFolderRect, {});
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
			const bool folderOpen = Icons::TreeNode("##folder", flags, Icons::Icon::Folder, folder.name.c_str(), 16.f);
			const bool recursivelyToggleChildren = ImGui::IsItemToggledOpen() && ImGui::GetIO().KeyAlt;
			if (ImGui::IsItemClicked())
			{
				selectFolder(folder.path.relative);
			}
			const ImRect folderRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			recordExternalDropFolder(folderRect, folder.path.relative);
			DrawProjectAssetDragSource(selection, AssetSelection(folder), folder.name, inputAction);
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
			if (recursivelyToggleChildren)
			{
				setFolderNodeSubtreeOpen(folder, folderOpen, setFolderNodeSubtreeOpen);
			}
		};
		for (const ProjectBrowserEntry& folder : model.ChildFolders({}))
		{
			drawFolder(folder);
		}
		ImGui::TreePop();
	}
	if (recursivelyToggleAssets)
	{
		const ImGuiID assetsId = ImGui::GetID("Assets");
		ImGui::GetStateStorage()->SetInt(assetsId, assetsOpen ? 1 : 0);
		ImGui::PushID(assetsId);
		for (const ProjectBrowserEntry& folder : model.ChildFolders({}))
		{
			setFolderNodeSubtreeOpen(folder, assetsOpen, setFolderNodeSubtreeOpen);
		}
		ImGui::PopID();
	}
		ImGui::PopStyleVar();
		ImGui::EndChild();
		Icons::SameLine();
	}
	if (twoColumnLayout && columnFocusRequest == ColumnFocusRequest::Contents)
	{
		ImGui::SetNextWindowFocus();
		columnFocusRequest = ColumnFocusRequest::None;
		columnFocusMoved = true;
	}
	ImGui::BeginChild("ProjectFolderContents", ImVec2(0.0f, -projectFooterHeight), ImGuiChildFlags_Borders);
	const ImGuiIO& projectContentsInput = ImGui::GetIO();
	if (twoColumnLayout && !columnFocusMoved && ImGui::IsWindowFocused() && !projectContentsInput.WantTextInput &&
	    !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
	{
		columnFocusRequest = ColumnFocusRequest::FolderTree;
	}
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
	Icons::Inline(Icons::Icon::Search);
	const Float32 searchButtonsWidth = ImGui::CalcTextSize("Type").x + ImGui::CalcTextSize("Label").x +
	                                   ImGui::CalcTextSize("☆").x + ImGui::GetFrameHeight() +
	                                   ImGui::GetStyle().ItemSpacing.x * 5.f;
	ImGui::SetNextItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x - searchButtonsWidth));
	const ImGuiIO& projectInput = ImGui::GetIO();
	const bool focusProjectSearch = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	                                !projectInput.WantTextInput && (projectInput.KeyCtrl || projectInput.KeySuper) &&
	                                !projectInput.KeyAlt && !projectInput.KeyShift &&
	                                ImGui::IsKeyPressed(ImGuiKey_F, false);
	if (focusProjectSearch)
	{
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::InputTextWithHint("##ProjectSearch", "Search Assets (t:Type, l:Label)", searchText.data(), searchText.size());
	const bool projectSearchActive = ImGui::IsItemActive();
	ImGui::SameLine();
	auto foldSearchToken = [](String value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
		               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		return value;
	};
	auto hasSearchFilter = [&](const String& filter)
	{
		const String query(searchText.data());
		Size begin = 0;
		while (begin < query.size())
		{
			while (begin < query.size() && std::isspace(static_cast<unsigned char>(query[begin])))
			{
				++begin;
			}
			Size end = begin;
			while (end < query.size() && !std::isspace(static_cast<unsigned char>(query[end])))
			{
				++end;
			}
			if (foldSearchToken(query.substr(begin, end - begin)) == foldSearchToken(filter))
			{
				return true;
			}
			begin = end;
		}
		return false;
	};
	auto toggleSearchFilter = [&](const String& filter)
	{
		const String query(searchText.data());
		Vector<String> terms;
		bool removeFilter = false;
		Size begin = 0;
		while (begin < query.size())
		{
			while (begin < query.size() && std::isspace(static_cast<unsigned char>(query[begin])))
			{
				++begin;
			}
			Size end = begin;
			while (end < query.size() && !std::isspace(static_cast<unsigned char>(query[end])))
			{
				++end;
			}
			String term = query.substr(begin, end - begin);
			if (!term.empty() && foldSearchToken(term) == foldSearchToken(filter))
			{
				removeFilter = true;
			}
			else if (!term.empty())
			{
				terms.push_back(std::move(term));
			}
			begin = end;
		}
		if (!removeFilter)
		{
			terms.push_back(filter);
		}
		String updated;
		for (const String& term : terms)
		{
			if (!updated.empty())
			{
				updated.push_back(' ');
			}
			updated += term;
		}
		std::memset(searchText.data(), 0, searchText.size());
		std::memcpy(searchText.data(), updated.data(), std::min(updated.size(), searchText.size() - 1));
	};
	if (ImGui::SmallButton("Type"))
	{
		ImGui::OpenPopup("ProjectSearchTypeFilter");
	}
	if (ImGui::BeginPopup("ProjectSearchTypeFilter"))
	{
		Vector<std::pair<String, String>> types{{"Folder", "folder"},     {"Scene", "scene"},
		                                        {"Prefab", "prefab"},     {"Material", "material"},
		                                        {"Shader", "shader"},     {"Mesh", "mesh"},
		                                        {"Texture", "texture"},   {"Texture 2D", "texture2d"},
		                                        {"Script", "script"}};
		if (const auto* database = workspace.Database())
		{
			for (const ImportedArtifactRecord& object : database->VisibleImportedObjects())
			{
				if (!object.visibleInProject || object.kind.empty())
				{
					continue;
				}
				const String filter = foldSearchToken(object.kind);
				if (std::none_of(types.begin(), types.end(), [&](const auto& type) { return type.second == filter; }))
				{
					types.emplace_back(object.kind, filter);
				}
			}
		}
		std::sort(types.begin() + 9, types.end(), [](const auto& left, const auto& right)
		          { return left.first < right.first; });
		for (const auto& type : types)
		{
			const String filter = String("t:") + type.second;
			if (ImGui::MenuItem(type.first.c_str(), nullptr, hasSearchFilter(filter)))
			{
				toggleSearchFilter(filter);
			}
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Label"))
	{
		ImGui::OpenPopup("ProjectSearchLabelFilter");
	}
	if (ImGui::BeginPopup("ProjectSearchLabelFilter"))
	{
		Vector<String> labels;
		std::function<void(const String&)> collectLabels = [&](const String& folder)
		{
			for (const ProjectBrowserEntry& asset : model.ChildAssets(folder))
			{
				for (const String& label : asset.labels)
				{
					if (std::find(labels.begin(), labels.end(), label) == labels.end())
					{
						labels.push_back(label);
					}
				}
			}
			for (const ProjectBrowserEntry& child : model.ChildFolders(folder))
			{
				collectLabels(child.path.relative);
			}
		};
		collectLabels({});
		std::sort(labels.begin(), labels.end());
		if (labels.empty())
		{
			ImGui::TextDisabled("No asset labels in this project");
		}
		for (const String& label : labels)
		{
			const String filter = String("l:") + label;
			if (ImGui::MenuItem(label.c_str(), nullptr, hasSearchFilter(filter)))
			{
				toggleSearchFilter(filter);
			}
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	const String currentSearch(searchText.data());
	const bool isFavorite = !currentSearch.empty() &&
	                        std::find(favoriteSearches.begin(), favoriteSearches.end(), currentSearch) != favoriteSearches.end();
	if (ImGui::SmallButton(isFavorite ? "★" : "☆") && !currentSearch.empty() && !isFavorite)
	{
		favoriteSearches.push_back(currentSearch);
		saveProjectBrowserSettings();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip(isFavorite ? "Search is in Favorites" : "Add current search to Favorites");
	}
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
			for (const auto& object : asset.VisibleChildren(search))
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
	bool renameTargetRendered = false;
	bool renameCommitRequested = false;
	auto drawInlineRename = [&](const ProjectBrowserEntry& asset, ImVec2 minimum, ImVec2 maximum, bool grid)
	{
		if (!renameEditing || asset.path.relative != renameSourcePath)
		{
			return false;
		}
		renameTargetRendered = true;
		const ImVec2 layoutCursor = ImGui::GetCursorScreenPos();
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImGuiWindowTempData layoutState = window->DC;
		const ImVec2 inputPosition =
		    grid ? ImVec2{minimum.x + 5.f, minimum.y + static_cast<Float32>(gridIconSize) + 10.f}
		         : ImVec2{minimum.x + std::max(16.f, ImGui::GetFontSize()) + ImGui::GetStyle().ItemInnerSpacing.x,
		                  minimum.y};
		if (!grid)
		{
			const ImVec2 framePadding = ImGui::GetStyle().FramePadding;
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
			                    {framePadding.x,
			                     std::max(0.f, (maximum.y - minimum.y - ImGui::GetTextLineHeight()) * 0.5f)});
		}
		ImGui::SetCursorScreenPos(inputPosition);
		ImGui::SetNextItemWidth(std::max(72.f, maximum.x - inputPosition.x - (grid ? 10.f : 6.f)));
		if (renameFocusRequested)
		{
			ImGui::SetKeyboardFocusHere();
			renameFocusRequested = false;
		}
		if (ImGui::InputText("##ProjectAssetRename", renameName.data(), renameName.size(),
		                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
		{
			renameCommitRequested = true;
		}
		if (!grid)
		{
			ImGui::PopStyleVar();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			renameEditing = false;
			renameFocusRequested = false;
			renameSourcePath.clear();
		}
		window->DC = layoutState;
		ImGui::SetCursorScreenPos(layoutCursor);
		return true;
	};
	auto commitInlineRename = [&]
	{
		const ProjectBrowserEntry* asset = model.Find(renameSourcePath);
		const String newName(renameName.data());
		if (!asset || newName.empty() || newName.find_first_of("/\\") != String::npos || !actions.renameAsset)
		{
			diagnostic = "Enter a valid asset name";
			return;
		}
		String name = newName;
		if (!asset->folder && name.find('.') == String::npos)
		{
			const Size extension = asset->name.rfind('.');
			if (extension != String::npos && extension > 0)
			{
				name += asset->name.substr(extension);
			}
		}
		String relativePath = asset->path.relative;
		const Size separator = relativePath.rfind('/');
		relativePath = separator == String::npos ? name : relativePath.substr(0, separator + 1) + name;
		auto destination = VirtualPath::Parse(asset->path.scheme + ":" + relativePath);
		if (!destination)
		{
			diagnostic = destination.error;
			return;
		}
		auto renamed = actions.renameAsset(asset->path, destination.value);
		diagnostic = renamed ? String{} : renamed.error;
		if (renamed)
		{
			model.SelectAsset(destination.value);
			selection.SelectAsset(destination.value);
			hierarchy.ClearSelection();
			renameEditing = false;
			renameFocusRequested = false;
			renameSourcePath.clear();
		}
	};
	bool managedProjectFindHandled = managedProjectInputHandled && projectInputCommand == 5;
	if ((managedProjectFindHandled ||
	     (!managedProjectInputHandled && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	      !projectInput.WantTextInput && !projectInput.KeyCtrl && !projectInput.KeySuper && !projectInput.KeyAlt &&
	      !projectInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false))) &&
	    selection.entries.size() == 1)
	{
		const auto* primary = selection.PrimaryEntry();
		const auto* asset = primary && primary->kind == EditorSelectionKind::Asset && !primary->IsMissing() &&
		                            primary->assetRole == EditorAssetSelectionRole::Source && !primary->assetLocalFileId
		                        ? model.Find(primary->assetPath)
		                        : nullptr;
		if (asset)
		{
			String containingFolder = asset->path.relative;
			const Size separator = containingFolder.rfind('/');
			containingFolder = separator == String::npos ? String{} : containingFolder.substr(0, separator);
			std::fill(searchText.begin(), searchText.end(), '\0');
			auto navigated = model.NavigateTo(containingFolder);
			diagnostic = navigated ? String{} : navigated.error;
			if (navigated)
			{
				model.SelectAsset(asset->path);
				hierarchy.ClearSelection();
			}
		}
	}
	const bool searchNavigationKey = projectSearchActive && !projectInput.KeyCtrl && !projectInput.KeySuper &&
	                                 !projectInput.KeyAlt &&
	                                 (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) ||
	                                  ImGui::IsKeyPressed(ImGuiKey_DownArrow, false));
	std::optional<EditorSelectionEntry> projectKeyboardFocusTarget;
	if (ImGui::IsWindowFocused() && (!projectInput.WantTextInput || searchNavigationKey) && !projectInput.KeyAlt &&
	    !visibleRows.empty())
	{
		const bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow, false);
		const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow, false);
		const bool left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false);
		const bool right = ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
		const bool home = ImGui::IsKeyPressed(ImGuiKey_Home, false);
		const bool end = ImGui::IsKeyPressed(ImGuiKey_End, false);
		const bool pageUp = ImGui::IsKeyPressed(ImGuiKey_PageUp, false);
		const bool pageDown = ImGui::IsKeyPressed(ImGuiKey_PageDown, false);
		if (searchNavigationKey)
		{
			const auto* primary = selection.PrimaryEntry();
			const auto current = primary ? std::find_if(visibleRows.begin(), visibleRows.end(),
			                                           [&](const auto& entry) { return entry.SameIdentity(*primary); })
			                           : visibleRows.end();
			if (current != visibleRows.end())
			{
				projectKeyboardFocusTarget = *current;
			}
			else
			{
				projectKeyboardFocusTarget = visibleRows.front();
				if (selection.SelectEntries({*projectKeyboardFocusTarget}, 0, projectKeyboardFocusTarget))
				{
					model.SelectAsset(projectKeyboardFocusTarget->assetPath);
					hierarchy.ClearSelection();
				}
			}
		}
		else if (up || down || left || right || home || end || pageUp || pageDown)
		{
			const Float32 iconSize = static_cast<Float32>(gridIconSize);
			const Float32 tileWidth = std::clamp(iconSize + 26.f, 80.f, 152.f);
			const Float32 tileHeight = iconSize + 44.f;
			const Float32 spacing = ImGui::GetStyle().ItemSpacing.x;
			const Size columns =
			    gridView ? std::max<Size>(1, static_cast<Size>(std::floor((ImGui::GetContentRegionAvail().x + spacing) /
			                                                              (tileWidth + spacing))))
			             : 1;
			Size currentIndex = 0;
			bool hasVisibleSelection = false;
			if (const auto* primary = selection.PrimaryEntry())
			{
				const auto current = std::find_if(visibleRows.begin(), visibleRows.end(),
				                                  [&](const auto& entry) { return entry.SameIdentity(*primary); });
				if (current != visibleRows.end())
				{
					currentIndex = static_cast<Size>(std::distance(visibleRows.begin(), current));
					hasVisibleSelection = true;
				}
			}
			const Float32 rowHeight = gridView ? tileHeight : ImGui::GetFrameHeight();
			const Float32 rowStep = rowHeight + ImGui::GetStyle().ItemSpacing.y;
			const Int64 lastIndex = static_cast<Int64>(visibleRows.size() - 1);
			Int64 nextIndex = 0;
			if (!hasVisibleSelection)
			{
				nextIndex = 0;
			}
			else if (home)
			{
				nextIndex = 0;
			}
			else if (end)
			{
				nextIndex = lastIndex;
			}
			else if (pageUp || pageDown)
			{
				const Size pageRows =
				    std::max<Size>(1, static_cast<Size>(std::round(ImGui::GetContentRegionAvail().y / rowStep)));
				const Size maxPageRows =
				    pageDown ? (visibleRows.size() - 1 - currentIndex) / columns : currentIndex / columns;
				const Int64 pageOffset = static_cast<Int64>(std::min(pageRows, maxPageRows) * columns);
				nextIndex = static_cast<Int64>(currentIndex) + (pageDown ? pageOffset : -pageOffset);
			}
			else if (gridView && (up || down))
			{
				nextIndex = static_cast<Int64>(currentIndex) +
				            (down ? static_cast<Int64>(columns) : -static_cast<Int64>(columns));
			}
			else
			{
				nextIndex = static_cast<Int64>(currentIndex) + ((down || right) ? 1 : -1);
			}
			nextIndex = std::clamp<Int64>(nextIndex, 0, lastIndex);
			const Size targetIndex = static_cast<Size>(nextIndex);
			Vector<EditorSelectionEntry> nextSelection{visibleRows[targetIndex]};
			Size nextPrimary = 0;
			std::optional<EditorSelectionEntry> anchor = visibleRows[targetIndex];
			if (projectInput.KeyShift)
			{
				Size anchorIndex = currentIndex;
				if (selection.rangeAnchor)
				{
					const auto found = std::find_if(visibleRows.begin(), visibleRows.end(), [&](const auto& entry)
					                                { return entry.SameIdentity(*selection.rangeAnchor); });
					if (found != visibleRows.end())
					{
						anchorIndex = static_cast<Size>(std::distance(visibleRows.begin(), found));
						anchor = *found;
					}
				}
				const Size first = std::min(anchorIndex, targetIndex);
				const Size last = std::max(anchorIndex, targetIndex);
				nextSelection.assign(visibleRows.begin() + first, visibleRows.begin() + last + 1);
				nextPrimary = targetIndex - first;
			}
			if (selection.SelectEntries(std::move(nextSelection), nextPrimary, anchor))
			{
				if (const auto* primary = selection.PrimaryEntry();
				    primary && primary->kind == EditorSelectionKind::Asset)
				{
					model.SelectAsset(primary->assetPath);
					hierarchy.ClearSelection();
				}
				const Float32 targetTop =
				    ImGui::GetCursorPosY() +
				    static_cast<Float32>(gridView ? targetIndex / columns : targetIndex) * rowStep;
				const Float32 scroll = ImGui::GetScrollY();
				const Float32 viewportHeight =
				    ImGui::GetWindowHeight() - ImGui::GetWindowContentRegionMin().y - ImGui::GetStyle().WindowPadding.y;
				if (targetTop < scroll)
				{
					ImGui::SetScrollY(targetTop);
				}
				else if (targetTop + rowHeight > scroll + viewportHeight)
				{
					ImGui::SetScrollY(targetTop + rowHeight - viewportHeight);
				}
			}
		}
	}
	auto focusProjectEntry = [&](const EditorSelectionEntry& entry)
	{
		if (projectKeyboardFocusTarget && entry.SameIdentity(*projectKeyboardFocusTarget))
		{
			ImGui::SetKeyboardFocusHere();
			projectKeyboardFocusTarget.reset();
		}
	};
	Vector<ImGuiID> projectContentRowIds;
	projectContentRowIds.reserve(visibleRows.size());
	constexpr Float32 projectListRowHeight = 16.f;
	if (gridView)
	{
		const Float32 iconSize = static_cast<Float32>(gridIconSize);
		const Float32 tileWidth = std::clamp(iconSize + 26.f, 80.f, 152.f);
		const Float32 tileHeight = iconSize + 44.f;
		const Float32 spacing = ImGui::GetStyle().ItemSpacing.x;
		const Size columns = std::max<Size>(
		    1, static_cast<Size>(std::floor((ImGui::GetContentRegionAvail().x + spacing) / (tileWidth + spacing))));
		for (Size index = 0; index < visibleRows.size(); ++index)
		{
			const auto& entry = visibleRows[index];
			const ProjectBrowserEntry* asset = model.Find(entry.assetGuid);
			if (!asset)
			{
				continue;
			}
			String label = search.empty() ? asset->name : asset->path.relative;
			Icons::Icon icon = Icons::ForAsset(asset->folder, asset->kind, asset->importer, asset->path.relative);
			if (entry.assetLocalFileId)
			{
				const auto object = std::find_if(asset->importedObjects.begin(), asset->importedObjects.end(),
				                                 [&](const ImportedArtifactRecord& item)
				                                 { return item.localFileId == entry.assetLocalFileId; });
				if (object == asset->importedObjects.end())
				{
					continue;
				}
				label = object->Label();
				const auto kind = ResourceKindForPath(object->path);
				icon = Icons::ForAsset(false, kind ? std::optional<ResourceKind>(kind.value) : std::nullopt, {}, {});
			}
			if (index % columns != 0)
			{
				ImGui::SameLine(0.f, spacing);
			}
			ImGui::PushID(entry.assetGuid.ToString().c_str());
			if (entry.assetLocalFileId)
			{
				const String localId = ToUrsaString(std::to_string(entry.assetLocalFileId));
				ImGui::PushID(localId.c_str());
			}
			focusProjectEntry(entry);
			const bool clicked = ImGui::Selectable("##ProjectAssetTile", selection.Contains(entry),
			                                       ImGuiSelectableFlags_AllowDoubleClick, {tileWidth, tileHeight});
			projectContentRowIds.push_back(ImGui::GetItemID());
			const ImVec2 minimum = ImGui::GetItemRectMin();
			const ImVec2 maximum = ImGui::GetItemRectMax();
			if (asset->folder && !entry.assetLocalFileId)
			{
				recordExternalDropFolder(ImRect(minimum, maximum), asset->path.relative);
			}
			UInt32 thumbnailWidth = 0;
			UInt32 thumbnailHeight = 0;
			const ImTextureID thumbnailId =
			    !entry.assetLocalFileId ? ResolveTextureThumbnail(workspace, *asset, thumbnailWidth, thumbnailHeight)
			                            : ImTextureID_Invalid;
			if (thumbnailId != ImTextureID_Invalid && thumbnailWidth > 0 && thumbnailHeight > 0)
			{
				const Float32 thumbnailScale = std::min(iconSize / static_cast<Float32>(thumbnailWidth),
				                                        iconSize / static_cast<Float32>(thumbnailHeight));
				const Float32 drawWidth = static_cast<Float32>(thumbnailWidth) * thumbnailScale;
				const Float32 drawHeight = static_cast<Float32>(thumbnailHeight) * thumbnailScale;
				const ImVec2 imageMinimum{minimum.x + (tileWidth - drawWidth) * 0.5f,
				                          minimum.y + 5.f + (iconSize - drawHeight) * 0.5f};
				ImGui::GetWindowDrawList()->AddImage(thumbnailId, imageMinimum,
				                                     {imageMinimum.x + drawWidth, imageMinimum.y + drawHeight});
			}
			else
			{
				Icons::Draw(icon, {minimum.x + (tileWidth - iconSize) * 0.5f, minimum.y + 5.f}, iconSize,
				            Icons::Color(icon));
			}
			const Float32 textTop = minimum.y + iconSize + 10.f;
			const bool editingName = !entry.assetLocalFileId && drawInlineRename(*asset, minimum, maximum, true);
			if (!editingName)
			{
				ImGui::GetWindowDrawList()->PushClipRect({minimum.x + 4.f, textTop}, {maximum.x - 4.f, maximum.y - 2.f},
				                                         true);
				ImGui::RenderTextWrapped({minimum.x + 5.f, textTop}, label.c_str(), nullptr, tileWidth - 10.f);
				ImGui::GetWindowDrawList()->PopClipRect();
			}
			if (clicked && !editingName)
			{
				const bool accepted = selectAsset(entry);
				if (accepted && !entry.assetLocalFileId)
				{
					model.SelectAsset(asset->path);
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						if (asset->folder)
						{
							selectFolder(asset->path.relative);
							selection.Clear();
							model.ClearAssetSelection();
						}
						else if (actions.openAsset)
						{
							auto opened = actions.openAsset(*asset);
							diagnostic = opened ? String{} : opened.error;
						}
					}
				}
			}
			if (!editingName)
			{
				DrawProjectAssetDragSource(selection, entry, label, inputAction);
			}
			if (!editingName && ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selection.Contains(entry))
			{
				if (selection.SelectEntries({entry}, 0, entry))
				{
					model.SelectAsset(asset->path);
					hierarchy.ClearSelection();
				}
			}
			if (!editingName && ImGui::BeginPopupContextItem("ProjectGridItemActions"))
			{
				if (entry.assetLocalFileId)
				{
					if (Icons::MenuItem("Select Source"))
					{
						const auto source = AssetSelection(*asset);
						selection.SelectEntries({source}, 0, source);
						model.SelectAsset(asset->path);
						hierarchy.ClearSelection();
					}
				}
				else
				{
					if (Icons::MenuItem(isFavoriteAsset(asset->path.relative) ? "Remove from Favorites"
					                                                          : "Add to Favorites"))
					{
						toggleFavoriteAsset(asset->path.relative);
					}
					if (asset->folder && Icons::MenuItem("Open"))
					{
						selectFolder(asset->path.relative);
						selection.Clear();
						model.ClearAssetSelection();
					}
					if (!asset->folder && Icons::MenuItem("Open", nullptr, false, static_cast<bool>(actions.openAsset)))
					{
						auto opened = actions.openAsset(*asset);
						diagnostic = opened ? String{} : opened.error;
					}
					if (Icons::MenuItem("Rename Selected", nullptr, false,
					                    !readOnly && static_cast<bool>(actions.renameAsset)))
					{
						model.SelectAsset(asset->path);
						renameRequested = true;
					}
					if (Icons::MenuItem("Duplicate", "Ctrl+D", false,
					                    !readOnly && static_cast<bool>(actions.duplicateAssets)))
					{
						const EditorSelectionEntry source = AssetSelection(*asset);
						auto duplicated = duplicateEntries({source});
						diagnostic = duplicated ? String{} : duplicated.error;
					}
					if (Icons::MenuItem("Delete Selected", nullptr, false,
					                    !readOnly && static_cast<bool>(actions.deleteAssets)))
					{
						queueDelete();
					}
				}
				ImGui::EndPopup();
			}
			if (entry.assetLocalFileId)
			{
				ImGui::PopID();
			}
			ImGui::PopID();
		}
	}
	else
	{
		if (search.empty())
		{
			for (const ProjectBrowserEntry& folder : model.ChildFolders(currentFolder))
			{
				ImGui::PushID(folder.path.relative.c_str());
				const bool editingName = renameEditing && renameSourcePath == folder.path.relative;
				const auto folderEntry = AssetSelection(folder);
				focusProjectEntry(folderEntry);
				const bool folderClicked = Icons::Selectable(
				    Icons::Icon::Folder, editingName ? "" : folder.name.c_str(),
				    selection.Contains(folderEntry), ImGuiSelectableFlags_AllowDoubleClick);
				projectContentRowIds.push_back(ImGui::GetItemID());
				const ImVec2 folderMinimum = ImGui::GetItemRectMin();
				const ImVec2 folderMaximum = ImGui::GetItemRectMax();
				const ImRect folderRect(folderMinimum, folderMaximum);
				recordExternalDropFolder(folderRect, folder.path.relative);
				if (editingName)
				{
					drawInlineRename(folder, folderMinimum, folderMaximum, false);
				}
				if (folderClicked && !editingName)
				{
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						selectFolder(folder.path.relative);
					}
					else
					{
						selectAsset(folderEntry);
						model.SelectAsset(folder.path);
					}
				}
				if (!editingName)
				{
					DrawProjectAssetDragSource(selection, AssetSelection(folder), folder.name, inputAction);
				}
				DrawProjectMoveDropTarget(folderRect, workspace, model, selection, folder.path.relative, readOnly,
				                          diagnostic, actions);
				bool contextSelectionAccepted = true;
				if (!editingName && ImGui::IsItemClicked(ImGuiMouseButton_Right))
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
				if (!editingName && contextSelectionAccepted && ImGui::BeginPopupContextItem("FolderActions"))
				{
					if (Icons::MenuItem(isFavoriteAsset(folder.path.relative) ? "Remove from Favorites"
					                                                          : "Add to Favorites"))
					{
						toggleFavoriteAsset(folder.path.relative);
					}
					if (Icons::MenuItem("Cut", "Ctrl+X", false, canCopyCutSelection()))
					{
						auto cut = setAssetClipboard(selection.entries, true);
						diagnostic = cut ? String{} : cut.error;
					}
					if (Icons::MenuItem("Copy", "Ctrl+C", false, canCopyCutSelection()))
					{
						auto copied = setAssetClipboard(selection.entries, false);
						diagnostic = copied ? String{} : copied.error;
					}
					ImGui::BeginDisabled(readOnly || assetClipboard.empty() || assetClipboardProject != projectKey ||
					                     !actions.pasteAssets);
					if (Icons::MenuItem("Paste", "Ctrl+V"))
					{
						pasteRequestedFromMenu = true;
						pasteMenuDestination = folder.path.relative;
					}
					ImGui::EndDisabled();
					ImGui::Separator();
					if (Icons::MenuItem("Open"))
					{
						selectFolder(folder.path.relative);
					}
					Vector<VirtualPath> selectedLabelTargets;
					for (const EditorSelectionEntry& selected : selection.entries)
					{
						if (selected.kind == EditorSelectionKind::Asset && !selected.IsMissing() &&
						    selected.assetRole == EditorAssetSelectionRole::Source && !selected.assetLocalFileId &&
						    std::find(selectedLabelTargets.begin(), selectedLabelTargets.end(), selected.assetPath) ==
						        selectedLabelTargets.end())
						{
							selectedLabelTargets.push_back(selected.assetPath);
						}
					}
					if (Icons::MenuItem("Add Label...", nullptr, false, !readOnly && !selectedLabelTargets.empty()))
					{
						labelTargets = std::move(selectedLabelTargets);
						labelName.fill(0);
						labelDiagnostic.clear();
						addLabelRequested = true;
					}
					ImGui::BeginDisabled(readOnly);
					if (Icons::BeginMenu("Create"))
					{
						if (Icons::MenuItem("Folder") && actions.createFolder)
						{
							model.NavigateTo(folder.path.relative);
							requestCreateFolder();
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
						    return actions.renameAsset && !readOnly && context.selection.entries.size() == 1 &&
						                   primary && primary->kind == EditorSelectionKind::Asset &&
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
						            ? PlanProjectSourceDelete(*workspace.Database(), context.selection.entries,
						                                      readOnly)
						            : Result<Vector<VirtualPath>>::Failure("Project AssetDatabase is unavailable");
						    return actions.deleteAssets && plan
						               ? Result<void>{}
						               : Result<void>::Failure(plan ? "Project Delete is unavailable" : plan.error);
					    });
					if (Icons::MenuItem("Duplicate", "Ctrl+D", false,
					                    static_cast<bool>(actions.duplicateAssets) && !readOnly &&
					                        std::all_of(selection.entries.begin(), selection.entries.end(),
					                                    [](const EditorSelectionEntry& entry)
					                                    {
						                                    return entry.kind == EditorSelectionKind::Asset &&
						                                                   entry.assetRole == EditorAssetSelectionRole::Source &&
						                                                   !entry.assetLocalFileId && !entry.IsMissing();
					                                    })))
					{
						auto duplicated = duplicateEntries(selection.entries);
						diagnostic = duplicated ? String{} : duplicated.error;
					}
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
					    workspace.Database()
					        ? PlanProjectSourceReimport(*workspace.Database(), selection.entries, readOnly)
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
							               : Result<void>::Failure(plan ? "Project Reimport is unavailable"
							                                            : plan.error);
						    },
						    [&](const EditorCommandContext& context)
						    {
							    auto plan = PlanProjectSourceReimport(*workspace.Database(), context.selection.entries,
							                                          readOnly);
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
			const auto visibleObjects = asset.VisibleChildren(search);
			const bool hasChildren = !visibleObjects.empty();
			if (hasChildren)
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {2.f, 1.5f});
				if (ImGui::ArrowButton("##ImportedObjects", expanded ? ImGuiDir_Down : ImGuiDir_Right))
				{
					expanded = !expanded;
				}
				ImGui::PopStyleVar();
				ImGui::SameLine();
			}
			const auto assetEntry = AssetSelection(asset);
			const bool selected = selection.Contains(assetEntry);
			const String label = search.empty() ? asset.name : asset.path.relative;
			const bool editingName =
			    !assetEntry.assetLocalFileId && renameEditing && renameSourcePath == asset.path.relative;
			focusProjectEntry(assetEntry);
			const bool assetClicked =
			    Icons::Selectable(Icons::ForAsset(false, asset.kind, asset.importer, asset.path.relative),
			                      editingName ? "" : label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick,
			                      {0.f, projectListRowHeight});
			projectContentRowIds.push_back(ImGui::GetItemID());
			const ImVec2 assetMinimum = ImGui::GetItemRectMin();
			const ImVec2 assetMaximum = ImGui::GetItemRectMax();
			if (editingName)
			{
				drawInlineRename(asset, assetMinimum, assetMaximum, false);
			}
			if (assetClicked && !editingName)
			{
				const bool accepted = selectAsset(assetEntry);
				if (accepted)
				{
					model.SelectAsset(asset.path);
				}
				if (accepted && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && actions.openAsset)
				{
					EditorCommands commands;
					commands.Register({"project.open",
					                   "Open",
					                   {},
					                   [assetEntry](const EditorCommandContext& context)
					                   {
						                   const auto* primary = context.selection.PrimaryEntry();
						                   return primary && primary->SameIdentity(assetEntry) && !primary->IsMissing()
						                              ? Result<void>{}
						                              : Result<void>::Failure(
						                                    "The Project selection changed before opening the asset");
					                   },
					                   [&actions, &asset](const EditorCommandContext&)
					                   { return actions.openAsset(asset); }});
					const auto commandContext =
					    EditorCommandContext::Capture(selection, selection.activeDocumentId, selection.revision);
					auto opened = commands.Execute("project.open", commandContext, commandContext);
					diagnostic = opened ? String{} : opened.error;
				}
			}
			if (!editingName)
			{
				DrawProjectAssetDragSource(selection, assetEntry, asset.name, inputAction);
			}
			bool contextSelectionAccepted = true;
			if (!editingName && ImGui::IsItemClicked(ImGuiMouseButton_Right))
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
			if (!editingName && contextSelectionAccepted && ImGui::BeginPopupContextItem("AssetActions"))
			{
				if (Icons::MenuItem(isFavoriteAsset(asset.path.relative) ? "Remove from Favorites" : "Add to Favorites"))
				{
					toggleFavoriteAsset(asset.path.relative);
				}
				if (Icons::MenuItem("Cut", "Ctrl+X", false, canCopyCutSelection()))
				{
					auto cut = setAssetClipboard(selection.entries, true);
					diagnostic = cut ? String{} : cut.error;
				}
				if (Icons::MenuItem("Copy", "Ctrl+C", false, canCopyCutSelection()))
				{
					auto copied = setAssetClipboard(selection.entries, false);
					diagnostic = copied ? String{} : copied.error;
				}
				ImGui::BeginDisabled(readOnly || assetClipboard.empty() || assetClipboardProject != projectKey ||
				                     !actions.pasteAssets);
				if (Icons::MenuItem("Paste", "Ctrl+V"))
				{
					pasteRequestedFromMenu = true;
					pasteMenuDestination = model.CurrentFolder();
				}
				ImGui::EndDisabled();
				ImGui::Separator();
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
				const auto labelTargetsForSelection = [&]()
				{
					Vector<VirtualPath> paths;
					for (const EditorSelectionEntry& selected : selection.entries)
					{
						if (selected.kind == EditorSelectionKind::Asset && !selected.IsMissing() &&
						    selected.assetRole == EditorAssetSelectionRole::Source && !selected.assetLocalFileId &&
						    std::find(paths.begin(), paths.end(), selected.assetPath) == paths.end())
						{
							paths.push_back(selected.assetPath);
						}
					}
					return paths;
				};
				const auto selectedLabelTargets = labelTargetsForSelection();
				if (Icons::MenuItem("Add Label...", nullptr, false, !readOnly && !selectedLabelTargets.empty()))
				{
					labelTargets = selectedLabelTargets;
					labelName.fill(0);
					labelDiagnostic.clear();
					addLabelRequested = true;
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
						    return actions.renameAsset && !readOnly && context.selection.entries.size() == 1 &&
						                   primary && primary->kind == EditorSelectionKind::Asset &&
						                   !primary->IsMissing() &&
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
					focusProjectEntry(objectEntry);
					if (Icons::Selectable(Icons::ForAsset(false,
					                                      resourceKind ? std::optional<ResourceKind>(resourceKind.value)
					                                                   : std::nullopt,
					                                      {}, {}),
					                      objectLabel.c_str(),
					                      selection.Contains(AssetSelection(asset, object.localFileId, object.kind)), 0,
					                      {0.f, projectListRowHeight}))
					{
						if (selectAsset(AssetSelection(asset, object.localFileId, object.kind)))
						{
						}
					}
					projectContentRowIds.push_back(ImGui::GetItemID());
					DrawProjectAssetDragSource(selection, objectEntry, objectLabel, inputAction);
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
								               ? (model.SelectAsset(asset.path), hierarchy.ClearSelection(),
								                  Result<void>{})
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
	}
	const ImGuiID focusedItemId = ImGui::GetFocusID();
	const bool projectContentRowFocused = focusedItemId != 0 &&
	                                      std::find(projectContentRowIds.begin(), projectContentRowIds.end(),
	                                                focusedItemId) != projectContentRowIds.end();
	contentRowKeyboardFocused = projectContentRowFocused;
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && projectContentRowFocused &&
	    !projectInput.WantTextInput && !ImGui::IsAnyItemActive() && projectInput.KeyCtrl &&
	    ImGui::IsKeyPressed(ImGuiKey_A, false) && !visibleRows.empty())
	{
		if (selection.SelectEntries(visibleRows, visibleRows.size() - 1, visibleRows.back()))
		{
			model.SelectAsset(visibleRows.back().assetPath);
			hierarchy.ClearSelection();
		}
	}
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && projectContentRowFocused &&
	    !projectInput.WantTextInput && !ImGui::IsAnyItemActive() &&
	    (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) &&
	    selection.entries.size() == 1 && actions.openAsset)
	{
		const auto* primary = selection.PrimaryEntry();
		const bool visibleSelection =
		    primary && primary->kind == EditorSelectionKind::Asset && !primary->IsMissing() &&
		    primary->assetRole == EditorAssetSelectionRole::Source && !primary->assetLocalFileId &&
		    std::any_of(visibleRows.begin(), visibleRows.end(),
			            [&](const EditorSelectionEntry& row) { return row.SameIdentity(*primary); });
		const auto* asset = visibleSelection ? model.Find(primary->assetPath) : nullptr;
		if (asset && asset->folder)
		{
			selectFolder(asset->path.relative);
		}
		else if (asset)
		{
			auto opened = actions.openAsset(*asset);
			diagnostic = opened ? String{} : opened.error;
		}
	}
	if (renameEditing && !renameTargetRendered)
	{
		renameEditing = false;
		renameFocusRequested = false;
		renameSourcePath.clear();
	}
	if (renameCommitRequested)
	{
		commitInlineRename();
	}
	if (!readOnly && actions.importExternalFiles)
	{
		for (Size index = 0; index < externalDropPositions.size(); ++index)
		{
			if (!externalDropAccepted[index])
			{
				continue;
			}
			auto imported = actions.importExternalFiles(externalDrops[index].paths, externalDropDestinations[index]);
			if (!imported)
			{
				diagnostic = imported.error;
			}
		}
	}
	const ImVec2 blankDropMin = ImGui::GetCursorScreenPos();
	const ImRect contentWork = ImGui::GetCurrentWindow()->WorkRect;
	if (twoColumnLayout && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	    !ImGui::GetIO().WantTextInput &&
	    ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
	{
		const String path = model.CurrentFolder();
		if (!path.empty())
		{
			const Size separator = path.rfind('/');
			selectFolder(separator == String::npos ? String{} : path.substr(0, separator));
		}
	}
	if (!selection.entries.empty() && !ImGui::IsAnyItemHovered() &&
	    ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
	    ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetDragDropPayload() && selection.Clear())
	{
		model.ClearAssetSelection();
		hierarchy.ClearSelection();
	}
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
			                   requestCreateFolder();
			                   return Result<void>{};
		                   }});
		ImGui::BeginDisabled(readOnly || assetClipboard.empty() || assetClipboardProject != projectKey ||
		                     !actions.pasteAssets);
		if (Icons::MenuItem("Paste", "Ctrl+V"))
		{
			pasteRequestedFromMenu = true;
			pasteMenuDestination = targetFolder;
		}
		ImGui::EndDisabled();
		ImGui::Separator();
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
	if (pasteRequestedFromMenu)
	{
		auto pasted = pasteAssetClipboard(pasteMenuDestination);
		diagnostic = pasted ? String{} : pasted.error;
	}
	ImGui::Separator();
	String footerPath = currentFolder.empty() ? String("Assets") : String("Assets/") + currentFolder;
	if (const auto* primary = selection.PrimaryEntry(); primary && primary->kind == EditorSelectionKind::Asset &&
	    !primary->assetPath.relative.empty())
	{
		footerPath = primary->assetPath.relative;
	}
	const float footerWidth = ImGui::GetContentRegionAvail().x;
	const float iconSliderWidth = std::min(160.0f, std::max(72.0f, footerWidth * 0.3f));
	ImGui::TextDisabled("%s", footerPath.c_str());
	ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - iconSliderWidth));
	ImGui::SetNextItemWidth(iconSliderWidth);
	Int32 iconScale = gridView ? gridIconSize : 16;
	if (ImGui::SliderInt("##ProjectIconSize", &iconScale, 16, 96, iconScale == 16 ? "List" : "Icon %d"))
	{
		gridView = iconScale >= 20;
		if (gridView)
		{
			gridIconSize = std::max(32, iconScale);
		}
	}
	if (deleteRequested)
	{
		ImGui::OpenPopup("Delete Project Assets");
	}
	if (addLabelRequested)
	{
		ImGui::OpenPopup("Add Asset Label");
	}
	if (ImGui::BeginPopupModal("Add Asset Label", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Add one label to %zu source asset(s).", labelTargets.size());
		ImGui::SetNextItemWidth(280.0f);
		const bool submit = ImGui::InputTextWithHint("##AssetLabel", "Label (no spaces or colon)", labelName.data(),
		                                             labelName.size(), ImGuiInputTextFlags_EnterReturnsTrue);
		if (!labelDiagnostic.empty())
		{
			ImGui::TextWrapped("%s", labelDiagnostic.c_str());
		}
		if ((ImGui::Button("Add", ImVec2(120.0f, 0.0f)) || submit) && workspace.Database() && !readOnly)
		{
			Result<void> added;
			for (const VirtualPath& path : labelTargets)
			{
				added = workspace.Database()->AddLabel(path, String(labelName.data()));
				if (!added)
				{
					break;
				}
			}
			if (!added)
			{
				labelDiagnostic = added.error;
			}
			else
			{
				auto refreshed = model.Refresh(workspace);
				labelDiagnostic = refreshed ? String{} : refreshed.error;
				if (refreshed)
				{
					addLabelRequested = false;
					labelTargets.clear();
					ImGui::CloseCurrentPopup();
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
		{
			addLabelRequested = false;
			labelTargets.clear();
			labelDiagnostic.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
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

} // namespace Ursa::Editor
