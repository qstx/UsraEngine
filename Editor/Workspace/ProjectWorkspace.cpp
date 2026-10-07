#include <Workspace/ProjectWorkspace.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <algorithm>
#include <cwctype>

namespace Ursa::Editor
{
namespace
{
bool SamePart(const std::filesystem::path& left, const std::filesystem::path& right)
{
#ifdef _WIN32
	const auto a = left.wstring();
	const auto b = right.wstring();
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y)
	                                          { return std::towlower(x) == std::towlower(y); });
#else
	return left == right;
#endif
}

bool ContainedIn(const std::filesystem::path& root, const std::filesystem::path& path)
{
	auto part = root.begin();
	auto candidate = path.begin();
	for (; part != root.end(); ++part, ++candidate)
	{
		if (candidate == path.end() || !SamePart(*part, *candidate))
		{
			return false;
		}
	}
	return true;
}
} // namespace

Result<std::unique_ptr<ProjectWorkspace>> ProjectWorkspace::Open(const std::filesystem::path& projectDirectory,
                                                                 const Vector<ResourceMount>& builtinMounts)
{
	std::error_code error;
	const auto canonicalProject = std::filesystem::canonical(projectDirectory, error);
	if (error || !std::filesystem::is_directory(canonicalProject))
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure("Project directory is not a directory: " +
		                                                          PathToUrsaString(projectDirectory));
	}
	const auto canonicalAssets = std::filesystem::canonical(canonicalProject / "Assets", error);
	if (error || !std::filesystem::is_directory(canonicalAssets))
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure("Project has no readable assets directory: " +
		                                                          PathToUrsaString(canonicalProject / "Assets"));
	}

	auto workspace = std::unique_ptr<ProjectWorkspace>(new ProjectWorkspace());
	workspace->projectRoot = canonicalProject;
	workspace->assetsRoot = canonicalAssets;
	workspace->engineMounts = builtinMounts;

	auto added = workspace->index.Mount(ResourceMount{projectResourceScheme, canonicalAssets, MountAccess::ReadWrite});
	if (!added)
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure(added.error);
	}
	for (const ResourceMount& mount : builtinMounts)
	{
		const auto canonicalEngine = std::filesystem::canonical(mount.root, error);
		if (error || !std::filesystem::is_directory(canonicalEngine))
		{
			return Result<std::unique_ptr<ProjectWorkspace>>::Failure("Engine resource root is not a directory: " +
			                                                          PathToUrsaString(mount.root));
		}
		// The generated engine mirror legitimately lives in the project's cache
		// directory; only the writable assets folder must stay separate.
		if (ContainedIn(canonicalAssets, canonicalEngine) || ContainedIn(canonicalEngine, canonicalAssets))
		{
			return Result<std::unique_ptr<ProjectWorkspace>>::Failure(
			    "Project assets and Engine resource roots must be separate: " + PathToUrsaString(mount.root));
		}
		auto mounted = workspace->index.Mount(ResourceMount{mount.scheme, canonicalEngine, mount.access});
		if (!mounted)
		{
			return Result<std::unique_ptr<ProjectWorkspace>>::Failure(mounted.error);
		}
	}
	auto libraryProvider = DirectoryFileProvider::Open(canonicalProject / "Library", true);
	if (!libraryProvider)
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure(libraryProvider.error);
	}
	auto mountedLibrary = workspace->index.Mount(
	    ResourceMount{editorLibraryScheme, {}, MountAccess::ReadWrite, std::move(libraryProvider.value)});
	if (!mountedLibrary)
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure(mountedLibrary.error);
	}
	auto database = AssetDatabase::Open(workspace->index.Filesystem(), projectResourceScheme);
	if (!database)
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure(database.error);
	}
	workspace->assetDatabase = std::move(database.value);
	auto rebuilt = workspace->Refresh();
	if (!rebuilt)
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure(rebuilt.error);
	}
	return Result<std::unique_ptr<ProjectWorkspace>>::Success(std::move(workspace));
}

Result<void> ProjectWorkspace::Refresh()
{
	auto refreshedAssets = assetDatabase ? assetDatabase->Refresh() : Result<void>{};
	if (!refreshedAssets)
	{
		return refreshedAssets;
	}
	return index.Rebuild();
}

ResourceSession::AssetPathResolver ProjectWorkspace::AssetResolver() const
{
	return [this](ResourceId guid, Ursa::UInt64 localFileId)
	{
		if (assetDatabase)
		{
			return assetDatabase->Resolve(guid, localFileId);
		}
		if (localFileId != 0)
		{
			return Result<VirtualPath>::Failure("No AssetDatabase is available for sub-asset reference " +
			                                    guid.ToString());
		}
		auto location = index.Resolve(guid);
		return location ? Result<VirtualPath>::Success(location.value.path)
		                : Result<VirtualPath>::Failure(location.error);
	};
}

Result<std::unique_ptr<ProjectWorkspace>>
ProjectWorkspace::FromFilesystem(const std::filesystem::path& projectDirectory, const VirtualFileSystem& filesystem)
{
	auto workspace = std::unique_ptr<ProjectWorkspace>(new ProjectWorkspace());
	workspace->projectRoot = projectDirectory;
	workspace->assetsRoot = projectDirectory / "Assets";
	for (const auto& mount : filesystem.Providers())
	{
		auto added = workspace->index.Mount({mount.scheme, {}, mount.access, mount.provider});
		if (!added)
		{
			return Result<std::unique_ptr<ProjectWorkspace>>::Failure(added.error);
		}
	}
	const auto projectCheck = VirtualPath::Parse(String(projectResourceScheme) + ":.asset-database-check");
	if (projectCheck && filesystem.IsMounted(editorLibraryScheme) && filesystem.CheckWritable(projectCheck.value))
	{
		auto database = AssetDatabase::Open(workspace->index.Filesystem(), projectResourceScheme);
		if (!database)
		{
			return Result<std::unique_ptr<ProjectWorkspace>>::Failure(database.error);
		}
		workspace->assetDatabase = std::move(database.value);
	}
	auto rebuilt = workspace->Refresh();
	return rebuilt ? Result<std::unique_ptr<ProjectWorkspace>>::Success(std::move(workspace))
	               : Result<std::unique_ptr<ProjectWorkspace>>::Failure(rebuilt.error);
}

Result<void> ProjectWorkspace::CheckWritable(const VirtualPath& path) const
{
	return path.scheme == projectResourceScheme
	           ? index.Filesystem().CheckWritable(path)
	           : Result<void>::Failure("Only project resources are writable: " + path.ToString());
}
} // namespace Ursa::Editor
