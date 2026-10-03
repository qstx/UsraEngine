#pragma once

#include <core/Result.hpp>
#include <vfs/VirtualFileSystem.hpp>
#include <filesystem>

namespace ursa {
struct EngineAssetRootCandidates {
    // An explicit override wins whenever it is set, and must be readable.
    std::filesystem::path explicitRoot;
    // The running executable, used to derive "<executable directory>/assets".
    std::filesystem::path executablePath;
    // The build-time mirror produced next to compiled shaders.
    std::filesystem::path buildMirror;
};

// Resolves the engine asset root in the order explicit override, "assets" beside
// the executable, then the build-time mirror. Fails with a diagnostic naming
// every candidate when none is a readable directory.
Result<std::filesystem::path> resolveEngineAssetRoot(const EngineAssetRootCandidates &candidates);

// The ordered engine asset roots. An explicit override wins; otherwise "assets"
// beside the executable (a packaged layout) wins; otherwise the engine source
// assets followed by the generated mirror, which overlay to form one view.
Result<Vector<std::filesystem::path>> resolveEngineAssetRoots(const std::filesystem::path &explicitRoot = {});

// Mounts resolveEngineAssetRoots() read-only under the conventional engine
// scheme, so callers can read "ursa:..." paths.
Result<VirtualFileSystem> openEngineFileSystem(const std::filesystem::path &explicitRoot = {});

// The path of the running executable, or an empty path when it cannot be read.
std::filesystem::path currentExecutablePath();
} // namespace ursa
