#include <workspace/ProjectWorkspace.hpp>
#include <core/StringInterop.hpp>
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
                                                                 const Vector<ResourceMount>& engineMounts)
{
	std::error_code error;
	const auto canonicalProject = std::filesystem::canonical(projectDirectory, error);
	if (error || !std::filesystem::is_directory(canonicalProject))
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure("Project directory is not a directory: " +
		                                                          PathToUrsaString(projectDirectory));
	}
	const auto canonicalAssets = std::filesystem::canonical(canonicalProject / "assets", error);
	if (error || !std::filesystem::is_directory(canonicalAssets))
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure("Project has no readable assets directory: " +
		                                                          PathToUrsaString(canonicalProject / "assets"));
	}

	auto workspace = std::unique_ptr<ProjectWorkspace>(new ProjectWorkspace());
	workspace->projectRoot = canonicalProject;
	workspace->assetsRoot = canonicalAssets;
	workspace->engineMounts = engineMounts;

	auto added = workspace->index.Mount(ResourceMount{projectResourceScheme, canonicalAssets, MountAccess::readWrite});
	if (!added)
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure(added.error);
	}
	for (const ResourceMount& mount : engineMounts)
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
	auto rebuilt = workspace->index.Rebuild();
	if (!rebuilt)
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure(rebuilt.error);
	}
	return Result<std::unique_ptr<ProjectWorkspace>>::Success(std::move(workspace));
}

Result<void> ProjectWorkspace::Refresh()
{
	return index.Rebuild();
}

Result<std::filesystem::path> ProjectWorkspace::WritablePath(const VirtualPath& path) const
{
	if (path.scheme != projectResourceScheme)
	{
		return Result<std::filesystem::path>::Failure("Only project resources are writable, not " + path.ToString());
	}
	auto resolved = index.WritablePath(path);
	if (!resolved)
	{
		return Result<std::filesystem::path>::Failure(resolved.error);
	}
	return resolved;
}
} // namespace Ursa::Editor
