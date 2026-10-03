#include <workspace/ProjectWorkspace.hpp>
#include <core/StringInterop.hpp>
#include <algorithm>
#include <cwctype>

namespace ursa::editor {
namespace {
bool samePart(const std::filesystem::path &left, const std::filesystem::path &right) {
#ifdef _WIN32
    const auto a = left.wstring();
    const auto b = right.wstring();
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                               [](wchar_t x, wchar_t y) {
                                                   return std::towlower(x) == std::towlower(y);
                                               });
#else
    return left == right;
#endif
}

bool containedIn(const std::filesystem::path &root, const std::filesystem::path &path) {
    auto part = root.begin();
    auto candidate = path.begin();
    for (; part != root.end(); ++part, ++candidate)
        if (candidate == path.end() || !samePart(*part, *candidate))
            return false;
    return true;
}
} // namespace

Result<std::unique_ptr<ProjectWorkspace>> ProjectWorkspace::open(
    const std::filesystem::path &projectDirectory, const Vector<ResourceMount> &engineMounts) {
    std::error_code error;
    const auto canonicalProject = std::filesystem::canonical(projectDirectory, error);
    if (error || !std::filesystem::is_directory(canonicalProject))
        return Result<std::unique_ptr<ProjectWorkspace>>::failure(
            "Project directory is not a directory: " + pathToUrsaString(projectDirectory));
    const auto canonicalAssets = std::filesystem::canonical(canonicalProject / "assets", error);
    if (error || !std::filesystem::is_directory(canonicalAssets))
        return Result<std::unique_ptr<ProjectWorkspace>>::failure(
            "Project has no readable assets directory: " + pathToUrsaString(canonicalProject / "assets"));

    auto workspace = std::unique_ptr<ProjectWorkspace>(new ProjectWorkspace());
    workspace->projectRoot_ = canonicalProject;
    workspace->assetsRoot_ = canonicalAssets;
    workspace->engineMounts_ = engineMounts;

    auto added = workspace->index_.mount(
        ResourceMount{projectResourceScheme, canonicalAssets, MountAccess::ReadWrite});
    if (!added)
        return Result<std::unique_ptr<ProjectWorkspace>>::failure(added.error);
    for (const ResourceMount &mount : engineMounts) {
        const auto canonicalEngine = std::filesystem::canonical(mount.root, error);
        if (error || !std::filesystem::is_directory(canonicalEngine))
            return Result<std::unique_ptr<ProjectWorkspace>>::failure(
                "Engine resource root is not a directory: " + pathToUrsaString(mount.root));
        // The generated engine mirror legitimately lives in the project's cache
        // directory; only the writable assets folder must stay separate.
        if (containedIn(canonicalAssets, canonicalEngine) || containedIn(canonicalEngine, canonicalAssets))
            return Result<std::unique_ptr<ProjectWorkspace>>::failure(
                "Project assets and Engine resource roots must be separate: " + pathToUrsaString(mount.root));
        auto mounted = workspace->index_.mount(ResourceMount{mount.scheme, canonicalEngine, mount.access});
        if (!mounted)
            return Result<std::unique_ptr<ProjectWorkspace>>::failure(mounted.error);
    }
    auto rebuilt = workspace->index_.rebuild();
    if (!rebuilt)
        return Result<std::unique_ptr<ProjectWorkspace>>::failure(rebuilt.error);
    return Result<std::unique_ptr<ProjectWorkspace>>::success(std::move(workspace));
}

Result<void> ProjectWorkspace::refresh() {
    return index_.rebuild();
}

Result<std::filesystem::path> ProjectWorkspace::writablePath(const VirtualPath &path) const {
    if (path.scheme != projectResourceScheme)
        return Result<std::filesystem::path>::failure(
            "Only project resources are writable, not " + path.toString());
    auto resolved = index_.writablePath(path);
    if (!resolved)
        return Result<std::filesystem::path>::failure(resolved.error);
    return resolved;
}
} // namespace ursa::editor
