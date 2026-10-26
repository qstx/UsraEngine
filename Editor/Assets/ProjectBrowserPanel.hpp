#pragma once

#include <Assets/ProjectBrowserModel.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Workspace/EditorCommands.hpp>
#include <Workspace/EditorDragSession.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <Assets/AssetPreviewTexture.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <functional>

namespace Ursa::Editor
{
struct ProjectBrowserFileDrop
{
	Float64 x = 0.0;
	Float64 y = 0.0;
	Vector<String> paths;
};

struct ProjectBrowserActions
{
	std::function<Result<void>()> refresh;
	std::function<Result<void>(const String&)> createFolder;
	std::function<Result<void>(ResourceKind, MaterialType, const String&)> createAsset;
	const Vector<ScriptTypeDescriptor>* scriptableAssetTypes = nullptr;
	std::function<Result<void>(const ScriptTypeDescriptor&, const String&)> createScriptableAsset;
	std::function<Result<void>(const VirtualPath&, const VirtualPath&)> renameAsset;
	std::function<Result<void>(const Vector<AssetMoveRequest>&)> moveAssets;
	std::function<Result<void>(const Vector<VirtualPath>&)> deleteAssets;
	std::function<Result<void>(const Vector<VirtualPath>&)> reimportAssets;
	std::function<Result<Vector<VirtualPath>>(const Vector<VirtualPath>&)> duplicateAssets;
	std::function<Result<Vector<VirtualPath>>(const Vector<VirtualPath>&, const String&, bool)> pasteAssets;
	std::function<Result<void>(const ProjectBrowserEntry&)> openAsset;
	std::function<Result<void>(const EditorDragSession&, const String&)> exportScenePrefabs;
	std::function<Vector<ProjectBrowserFileDrop>()> takeExternalFileDrops;
	std::function<Result<void>(const Vector<String>&, const String&)> importExternalFiles;
};

class ProjectBrowserPanel final
{
public:
	~ProjectBrowserPanel();

	void Reset();

	void RequestRenameSelected();
	void RequestDeleteSelected(const EditorSelectionContext& selection);

	Result<void> Draw(ProjectWorkspace& workspace, ProjectBrowserModel& model, EditorSelectionContext& selection,
	                  EditorHierarchy& hierarchy, bool& open, bool readOnly, String& diagnostic,
	                  const ProjectBrowserActions& actions);

private:
	enum class ColumnFocusRequest
	{
		None,
		FolderTree,
		Contents
	};

	ImTextureID ResolveTextureThumbnail(ProjectWorkspace& workspace, const ProjectBrowserEntry& asset, UInt32& width,
	                                    UInt32& height);
	void ClearTextureThumbnails();
	void CaptureMenuContext(const String& key, const EditorSelectionContext& selection);
	Result<void> ExecuteMenuCommand(const String& key, const String& commandId, const String& label,
	                                const EditorSelectionContext& selection,
	                                const EditorCommandAvailability& availability,
	                                const EditorCommandAction& action) const;
	Result<void> CanExecuteMenuCommand(const String& key, const String& commandId, const String& label,
	                                   const EditorSelectionContext& selection,
	                                   const EditorCommandAvailability& availability) const;

	Map<String, bool> expandedAssets;
	Vector<String> favoriteSearches;
	Vector<String> favoriteAssetPaths;
	Vector<VirtualPath> assetClipboard;
	String assetClipboardProject;
	bool assetClipboardCut = false;
	VirtualFileSystem* settingsFiles = nullptr;
	VirtualPath favoritesSettingsPath;
	String favoritesSettingsProject;
	bool twoColumnLayout = true;
	bool contentRowKeyboardFocused = false;
	ColumnFocusRequest columnFocusRequest = ColumnFocusRequest::None;
	bool gridView = true;
	Int32 gridIconSize = 64;
	Ursa::Array<char, 256> searchText{};
	Ursa::Array<char, 256> destinationPath{};
	Ursa::Array<char, 128> folderName{};
	Ursa::Array<char, 128> labelName{};
	bool createFolderRequested = false;
	bool addLabelRequested = false;
	Vector<VirtualPath> labelTargets;
	String labelDiagnostic;
	bool renameRequested = false;
	bool renameEditing = false;
	bool renameFocusRequested = false;
	String renameSourcePath;
	Ursa::Array<char, 256> renameName{};
	bool deleteRequested = false;
	Vector<EditorSelectionEntry> deleteTargets;
	String deleteDiagnostic;
	String backgroundMenuFolder;
	Map<String, EditorCommandContext> contextMenuTargets;

	struct TextureThumbnail
	{
		ImTextureID id = ImTextureID_Invalid;
		Rhi::TexturePtr texture;
		Rhi::SamplerPtr sampler;
		UInt32 width = 0;
		UInt32 height = 0;
		bool attempted = false;
	};

	Map<String, TextureThumbnail> textureThumbnails;
	ImGuiBridge* thumbnailBridge = nullptr;
	String thumbnailProjectRoot;
	UInt64 thumbnailDatabaseRevision = 0;
};
} // namespace Ursa::Editor
