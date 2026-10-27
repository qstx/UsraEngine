#include <Assets/ExternalProjectImport.hpp>
#include <Core/StringInterop.hpp>
#include <Assets/ProjectBrowserModel.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <Workspace/ProjectWorkspace.hpp>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace Ursa::Editor
{
namespace
{
bool IsLink(const std::filesystem::path& path, const std::filesystem::file_status& status, std::error_code& error)
{
	if (std::filesystem::is_symlink(status))
	{
		return true;
	}
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
		return false;
	}
	return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
	(void)path;
	(void)error;
	return false;
#endif
}
} // namespace

Result<void> CopyExternalProjectFiles(const std::filesystem::path& root, const Vector<String>& paths,
                                      const String& destinationFolder, bool* copiedAnything)
{
	if (copiedAnything)
	{
		*copiedAnything = false;
	}
	const std::filesystem::path assetsRoot = root.lexically_normal();
	const std::filesystem::path destination = (assetsRoot / ToStdString(destinationFolder)).lexically_normal();
	const std::filesystem::path relativeDestination = destination.lexically_relative(assetsRoot);
	if (relativeDestination.empty() || relativeDestination.is_absolute() ||
	    *relativeDestination.begin() == std::filesystem::path(".."))
	{
		return Result<void>::Failure("The Project drop destination is outside the Assets folder");
	}
	std::error_code error;
	const auto canonicalAssetsRoot = std::filesystem::weakly_canonical(assetsRoot, error);
	if (error)
	{
		return Result<void>::Failure("Could not resolve the Assets folder: " + ToUrsaString(error.message()));
	}
	const auto canonicalDestination = std::filesystem::weakly_canonical(destination, error);
	if (error || canonicalDestination.lexically_relative(canonicalAssetsRoot).empty() ||
	    *canonicalDestination.lexically_relative(canonicalAssetsRoot).begin() == std::filesystem::path(".."))
	{
		return Result<void>::Failure("The Project drop destination resolves outside the Assets folder");
	}
	// Reject a source containing the destination before creating any output: otherwise
	// recursive traversal can discover and repeatedly copy its own newly created folder.
	for (const String& droppedPath : paths)
	{
		const std::filesystem::path source = std::filesystem::path(ToStdString(droppedPath)).lexically_normal();
		const auto sourceStatus = std::filesystem::symlink_status(source, error);
		if (error || !std::filesystem::exists(sourceStatus) || IsLink(source, sourceStatus, error) || error)
		{
			return Result<void>::Failure("Could not read dropped path: " + droppedPath);
		}
		if (std::filesystem::is_directory(sourceStatus))
		{
			const auto canonicalSource = std::filesystem::weakly_canonical(source, error);
			if (error)
			{
				return Result<void>::Failure("Could not resolve dropped folder: " + droppedPath);
			}
			const auto relative = canonicalDestination.lexically_relative(canonicalSource);
			if (!relative.empty() && !relative.is_absolute() && *relative.begin() != std::filesystem::path(".."))
			{
				return Result<void>::Failure("The Project drop destination is inside the dropped folder: " +
				                             droppedPath);
			}
		}
	}
	std::filesystem::create_directories(destination, error);
	if (error)
	{
		return Result<void>::Failure("Could not create the Project drop folder: " + ToUrsaString(error.message()));
	}
	auto uniquePath = [](const std::filesystem::path& requested)
	{
		auto available = [](const std::filesystem::path& candidate)
		{ return !std::filesystem::exists(candidate) && !std::filesystem::exists(candidate.string() + ".meta"); };
		if (available(requested))
		{
			return requested;
		}
		const auto parent = requested.parent_path();
		const auto extension = requested.has_extension() ? requested.extension() : std::filesystem::path{};
		const auto stem = requested.has_extension() ? requested.stem() : requested.filename();
		for (UInt32 suffix = 1;; ++suffix)
		{
			const auto candidate = parent / (stem.string() + " (" + std::to_string(suffix) + ")" + extension.string());
			if (available(candidate))
			{
				return candidate;
			}
		}
	};
	for (const String& droppedPath : paths)
	{
		const std::filesystem::path source = std::filesystem::path(ToStdString(droppedPath)).lexically_normal();
		const auto sourceStatus = std::filesystem::symlink_status(source, error);
		if (error || IsLink(source, sourceStatus, error) || error)
		{
			return Result<void>::Failure("Could not read dropped path: " + droppedPath);
		}
		if (std::filesystem::is_regular_file(sourceStatus))
		{
			const auto target = uniquePath(destination / source.filename());
			std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, error);
			if (error)
			{
				return Result<void>::Failure("Could not import " + droppedPath + ": " + ToUrsaString(error.message()));
			}
			if (copiedAnything)
			{
				*copiedAnything = true;
			}
			continue;
		}
		if (!std::filesystem::is_directory(sourceStatus))
		{
			continue;
		}
		const auto targetRoot = uniquePath(destination / source.filename());
		std::filesystem::create_directories(targetRoot, error);
		if (error)
		{
			return Result<void>::Failure("Could not create imported folder: " + ToUrsaString(error.message()));
		}
		if (copiedAnything)
		{
			*copiedAnything = true;
		}
		for (std::filesystem::recursive_directory_iterator
		         iterator(source, std::filesystem::directory_options::skip_permission_denied, error),
		     end;
		     iterator != end; iterator.increment(error))
		{
			if (error)
			{
				error.clear();
				continue;
			}
			const auto status = iterator->symlink_status(error);
			if (error || IsLink(iterator->path(), status, error) || error)
			{
				iterator.disable_recursion_pending();
				error.clear();
				continue;
			}
			const auto relative = std::filesystem::relative(iterator->path(), source, error);
			if (error)
			{
				return Result<void>::Failure("Could not resolve imported folder contents: " +
				                             ToUrsaString(error.message()));
			}
			const auto target = targetRoot / relative;
			if (std::filesystem::is_directory(status))
			{
				std::filesystem::create_directories(target, error);
			}
			else if (std::filesystem::is_regular_file(status))
			{
				std::filesystem::create_directories(target.parent_path(), error);
				if (!error)
				{
					std::filesystem::copy_file(iterator->path(), target, std::filesystem::copy_options::none, error);
				}
			}
			if (error)
			{
				return Result<void>::Failure("Could not import folder contents: " + ToUrsaString(error.message()));
			}
		}
	}
	return {};
}

Result<void> ImportExternalProjectFiles(ProjectWorkspace& workspace, ProjectBrowserModel& browser,
                                        EditorSelectionContext& selection, const Vector<String>& paths,
                                        const String& destinationFolder, const std::function<void()>& requestImport)
{
	bool copiedAnything = false;
	auto copied = CopyExternalProjectFiles(workspace.AssetsRoot(), paths, destinationFolder, &copiedAnything);
	if (!copied && !copiedAnything)
	{
		return copied;
	}
	if (requestImport)
	{
		requestImport();
	}
	auto refreshed = workspace.Refresh();
	if (refreshed)
	{
		refreshed = browser.Refresh(workspace);
		if (refreshed)
		{
			browser.ReconcileSelection(selection);
		}
	}
	if (!copied)
	{
		return Result<void>::Failure(
		    copied.error + (refreshed ? "; earlier files were copied and the Project was refreshed"
		                              : "; earlier files were copied but refresh failed: " + refreshed.error));
	}
	return refreshed;
}
} // namespace Ursa::Editor
