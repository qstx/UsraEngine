#pragma once

#include <Assets/ProjectAssetImporter.hpp>
#include <Workspace/ProjectWorkspace.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <Vfs/AssetRoots.hpp>

namespace Ursa::Editor
{
struct AssetImportOptions
{
	String configuration = EngineBuildConfiguration();
	String target = "WindowsVulkan";
	String profile = "Editor";
	VirtualPath scope;
	bool force = false;
	bool bootstrapOnly = false;
	bool buildScripts = true;
};

struct AssetImportReport
{
	std::shared_ptr<const AssetImportRules> rules;
	RuntimeContentSnapshot snapshot;
	VirtualFileSystem runtimeFiles;
	UInt64 importedAssets = 0;
	UInt64 cacheHits = 0;
};

class AssetImportPipeline final
{
public:
	using Progress = std::function<void(UInt64, UInt64, const VirtualPath&)>;

	static Result<std::unique_ptr<ProjectWorkspace>> OpenProject(const std::filesystem::path& project,
	                                                             const std::filesystem::path& builtinSource = {});

	static Result<AssetImportReport> Prepare(ProjectWorkspace& workspace, const AssetImportOptions& options = {},
	                                         Progress progress = {});

	static Result<String> SourceFingerprint(const ProjectWorkspace& workspace);

	static Result<String> SourceFingerprint(const VirtualFileSystem& files);
};
} // namespace Ursa::Editor
