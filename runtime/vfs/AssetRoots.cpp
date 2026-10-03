#include <vfs/AssetRoots.hpp>
#include <core/StringInterop.hpp>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace ursa {
namespace {
bool isReadableDirectory(const std::filesystem::path &path) {
    if (path.empty())
        return false;
    std::error_code error;
    return std::filesystem::is_directory(path, error) && !error;
}

std::filesystem::path engineAssetSource() {
#ifdef URSA_ENGINE_ASSET_SOURCE_DIR
    return std::filesystem::u8path(URSA_ENGINE_ASSET_SOURCE_DIR);
#else
    return {};
#endif
}

std::filesystem::path engineAssetMirror() {
#ifdef URSA_ASSET_MIRROR_DIR
    return std::filesystem::u8path(URSA_ASSET_MIRROR_DIR);
#else
    return {};
#endif
}
} // namespace

Result<std::filesystem::path> resolveEngineAssetRoot(const EngineAssetRootCandidates &candidates) {
    if (!candidates.explicitRoot.empty()) {
        if (!isReadableDirectory(candidates.explicitRoot))
            return Result<std::filesystem::path>::failure("Engine asset root is not a readable directory: " +
                                                          pathToUrsaString(candidates.explicitRoot));
        return Result<std::filesystem::path>::success(candidates.explicitRoot);
    }

    const std::filesystem::path besideExecutable =
        candidates.executablePath.empty() ? std::filesystem::path{}
                                          : candidates.executablePath.parent_path() / "assets";
    if (isReadableDirectory(besideExecutable))
        return Result<std::filesystem::path>::success(besideExecutable);

    if (isReadableDirectory(candidates.buildMirror))
        return Result<std::filesystem::path>::success(candidates.buildMirror);

    return Result<std::filesystem::path>::failure(
        "No engine asset root found. Tried an explicit override, " + pathToUrsaString(besideExecutable) + ", and " +
        pathToUrsaString(candidates.buildMirror));
}

Result<Vector<std::filesystem::path>> resolveEngineAssetRoots(const std::filesystem::path &explicitRoot) {
    if (!explicitRoot.empty()) {
        if (!isReadableDirectory(explicitRoot))
            return Result<Vector<std::filesystem::path>>::failure(
                "Engine asset root is not a readable directory: " + pathToUrsaString(explicitRoot));
        Vector<std::filesystem::path> single;
        single.push_back(explicitRoot);
        return Result<Vector<std::filesystem::path>>::success(std::move(single));
    }

    const auto executable = currentExecutablePath();
    const std::filesystem::path besideExecutable =
        executable.empty() ? std::filesystem::path{} : executable.parent_path() / "assets";
    if (isReadableDirectory(besideExecutable)) {
        Vector<std::filesystem::path> packaged;
        packaged.push_back(besideExecutable);
        return Result<Vector<std::filesystem::path>>::success(std::move(packaged));
    }

    const auto source = engineAssetSource();
    const auto mirror = engineAssetMirror();
    Vector<std::filesystem::path> roots;
    if (isReadableDirectory(source))
        roots.push_back(source);
    if (isReadableDirectory(mirror))
        roots.push_back(mirror);
    if (roots.empty())
        return Result<Vector<std::filesystem::path>>::failure(
            "No engine asset root found. Tried " + pathToUrsaString(besideExecutable) + ", " +
            pathToUrsaString(source) + ", and " + pathToUrsaString(mirror));
    return Result<Vector<std::filesystem::path>>::success(std::move(roots));
}

Result<VirtualFileSystem> openEngineFileSystem(const std::filesystem::path &explicitRoot) {
    auto roots = resolveEngineAssetRoots(explicitRoot);
    if (!roots)
        return Result<VirtualFileSystem>::failure(roots.error);
    VirtualFileSystem files;
    for (const std::filesystem::path &root : roots.value) {
        auto mounted = files.mount(engineScheme, root, MountAccess::ReadOnly);
        if (!mounted)
            return Result<VirtualFileSystem>::failure(mounted.error);
    }
    return Result<VirtualFileSystem>::success(std::move(files));
}

std::filesystem::path currentExecutablePath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size() - 1)
            return std::filesystem::path(std::wstring(buffer.data(), length));
        buffer.resize(buffer.size() * 2);
    }
#else
    std::vector<char> buffer(PATH_MAX);
    const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length <= 0)
        return {};
    return std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(length)));
#endif
}
} // namespace ursa
