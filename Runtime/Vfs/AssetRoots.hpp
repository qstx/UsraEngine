#pragma once

#include <Core/Result.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <filesystem>

namespace Ursa
{
struct EngineAssetRootCandidates
{
	// An explicit override wins whenever it is set, and must be readable.
	std::filesystem::path explicitRoot;
	// The running executable, used to derive "<executable directory>/Assets".
	std::filesystem::path executablePath;
	// Legacy discovery hint, ignored; generated resources belong to project Library.
	std::filesystem::path buildMirror;
};

// Resolves the engine asset root in the order explicit override, "Assets" beside
// the executable. buildMirror is ignored during migration. Fails with a diagnostic naming
// every candidate when none is a readable directory.
Result<std::filesystem::path> ResolveEngineAssetRoot(const EngineAssetRootCandidates& candidates);

// The ordered engine asset roots. An explicit override wins; otherwise "Assets"
// beside the executable (a packaged layout) wins; otherwise the engine source
// sources for authoring tools. Runtime consumers must use a prepared content view.
Result<Vector<std::filesystem::path>> ResolveEngineAssetRoots(const std::filesystem::path& explicitRoot = {});

Result<std::filesystem::path> ResolveEngineSourceRoot(const std::filesystem::path& explicitRoot = {});

String EngineBuildConfiguration();

std::filesystem::path EngineFrameworkDirectory();

// Mounts resolveEngineAssetRoots() read-only under the conventional engine
// scheme, so callers can read "ursa:..." paths.
Result<VirtualFileSystem> OpenEngineFileSystem(const std::filesystem::path& explicitRoot = {});

// Startup-only override shared by renderers and UI shader consumers.
void ConfigureEngineFileSystem(std::shared_ptr<const VirtualFileSystem> filesystem);

// The path of the running executable, or an empty path when it cannot be read.
std::filesystem::path CurrentExecutablePath();
} // namespace Ursa
