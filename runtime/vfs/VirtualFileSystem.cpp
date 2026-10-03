#include <vfs/VirtualFileSystem.hpp>
#include <core/StringInterop.hpp>
#include <algorithm>
#include <cwctype>
#include <fstream>

namespace ursa {
namespace {
bool samePart(const std::filesystem::path &left, const std::filesystem::path &right) {
#ifdef _WIN32
    const auto a = left.wstring();
    const auto b = right.wstring();
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towlower(x) == std::towlower(y);
           });
#else
    return left == right;
#endif
}

bool containedIn(const std::filesystem::path &root, const std::filesystem::path &candidate) {
    auto part = root.begin();
    auto entry = candidate.begin();
    for (; part != root.end(); ++part, ++entry)
        if (entry == candidate.end() || !samePart(*part, *entry))
            return false;
    return true;
}

String virtualRelative(const std::filesystem::path &root, const std::filesystem::path &path) {
    return pathToUrsaString(path.lexically_relative(root).generic_u8string());
}
} // namespace

Result<void> VirtualFileSystem::mount(const String &scheme, const std::filesystem::path &root,
                                      MountAccess access) {
    if (scheme.empty())
        return Result<void>::failure("Resource scheme name is empty");
    std::error_code error;
    const auto canonical = std::filesystem::canonical(root, error);
    if (error || !std::filesystem::is_directory(canonical))
        return Result<void>::failure("Resource mount is not a readable directory: " + pathToUrsaString(root));
    auto &list = mounts_[scheme];
    if (std::find_if(list.begin(), list.end(), [&](const Root &candidate) { return candidate.path == canonical; }) !=
        list.end())
        return {};
    list.push_back(Root{canonical, access});
    return {};
}

bool VirtualFileSystem::isMounted(const String &scheme) const {
    return findMount(scheme) != nullptr;
}

Vector<String> VirtualFileSystem::schemes() const {
    Vector<String> result;
    result.reserve(mounts_.size());
    for (const auto &entry : mounts_)
        result.push_back(entry.first);
    return result;
}

Result<Vector<std::filesystem::path>> VirtualFileSystem::roots(const String &scheme) const {
    const auto *mount = findMount(scheme);
    if (mount == nullptr)
        return Result<Vector<std::filesystem::path>>::failure("Resource scheme is not mounted: " + scheme);
    Vector<std::filesystem::path> result;
    result.reserve(mount->size());
    for (const Root &root : *mount)
        result.push_back(root.path);
    return Result<Vector<std::filesystem::path>>::success(std::move(result));
}

const Vector<VirtualFileSystem::Root> *VirtualFileSystem::findMount(const String &scheme) const {
    const auto found = mounts_.find(scheme);
    return found == mounts_.end() ? nullptr : &found->second;
}

Result<std::filesystem::path> VirtualFileSystem::resolveIn(const Root &root, const VirtualPath &path) const {
    std::error_code error;
    const auto candidate =
        std::filesystem::weakly_canonical(root.path / pathFromUrsaString(path.relative), error);
    if (error)
        return Result<std::filesystem::path>::failure("Cannot resolve " + path.toString() + ": " +
                                                      toUrsaString(error.message()));
    if (!containedIn(root.path, candidate))
        return Result<std::filesystem::path>::failure("Resource path escapes its mount root: " + path.toString());
    return Result<std::filesystem::path>::success(candidate);
}

Result<std::filesystem::path> VirtualFileSystem::resolveExisting(const VirtualPath &path) const {
    const auto *mount = findMount(path.scheme);
    if (mount == nullptr)
        return Result<std::filesystem::path>::failure("Resource scheme is not mounted: " + path.scheme);
    for (const Root &root : *mount) {
        auto candidate = resolveIn(root, path);
        if (!candidate)
            continue;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate.value, error) && !error)
            return Result<std::filesystem::path>::success(candidate.value);
    }
    return Result<std::filesystem::path>::failure("Resource not found: " + path.toString());
}

Result<Vector<uint8_t>> VirtualFileSystem::read(const VirtualPath &path) const {
    auto resolved = resolveExisting(path);
    if (!resolved)
        return Result<Vector<uint8_t>>::failure(resolved.error);
    std::ifstream input(resolved.value, std::ios::binary | std::ios::ate);
    if (!input)
        return Result<Vector<uint8_t>>::failure("Cannot open " + path.toString());
    const auto size = input.tellg();
    if (size < 0)
        return Result<Vector<uint8_t>>::failure("Cannot size " + path.toString());
    Vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    if (!bytes.empty())
        input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input)
        return Result<Vector<uint8_t>>::failure("Cannot read " + path.toString());
    return Result<Vector<uint8_t>>::success(std::move(bytes));
}

Result<String> VirtualFileSystem::readText(const VirtualPath &path) const {
    auto bytes = read(path);
    if (!bytes)
        return Result<String>::failure(bytes.error);
    return Result<String>::success(String(reinterpret_cast<const char *>(bytes.value.data()), bytes.value.size()));
}

Result<Vector<uint8_t>> VirtualFileSystem::readPrefix(const VirtualPath &path, uint64_t maxBytes) const {
    auto resolved = resolveExisting(path);
    if (!resolved)
        return Result<Vector<uint8_t>>::failure(resolved.error);
    std::ifstream input(resolved.value, std::ios::binary | std::ios::ate);
    if (!input)
        return Result<Vector<uint8_t>>::failure("Cannot open " + path.toString());
    const auto size = input.tellg();
    if (size < 0)
        return Result<Vector<uint8_t>>::failure("Cannot size " + path.toString());
    const uint64_t available = static_cast<uint64_t>(size);
    const uint64_t wanted = available < maxBytes ? available : maxBytes;
    Vector<uint8_t> bytes(static_cast<size_t>(wanted));
    input.seekg(0);
    if (!bytes.empty())
        input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input)
        return Result<Vector<uint8_t>>::failure("Cannot read " + path.toString());
    return Result<Vector<uint8_t>>::success(std::move(bytes));
}

Result<bool> VirtualFileSystem::exists(const VirtualPath &path) const {
    const auto *mount = findMount(path.scheme);
    if (mount == nullptr)
        return Result<bool>::failure("Resource scheme is not mounted: " + path.scheme);
    for (const Root &root : *mount) {
        auto candidate = resolveIn(root, path);
        if (!candidate)
            continue;
        std::error_code error;
        const bool file = std::filesystem::is_regular_file(candidate.value, error) && !error;
        if (file)
            return Result<bool>::success(true);
    }
    return Result<bool>::success(false);
}

Result<uint64_t> VirtualFileSystem::size(const VirtualPath &path) const {
    auto resolved = resolveExisting(path);
    if (!resolved)
        return Result<uint64_t>::failure(resolved.error);
    std::error_code error;
    const auto bytes = std::filesystem::file_size(resolved.value, error);
    if (error)
        return Result<uint64_t>::failure("Cannot read the size of " + path.toString() + ": " +
                                         toUrsaString(error.message()));
    return Result<uint64_t>::success(static_cast<uint64_t>(bytes));
}

Result<std::filesystem::path> VirtualFileSystem::resolveToDisk(const VirtualPath &path) const {
    return resolveExisting(path);
}

Result<std::filesystem::path> VirtualFileSystem::resolveForWrite(const VirtualPath &path) const {
    const auto *mount = findMount(path.scheme);
    if (mount == nullptr || mount->empty())
        return Result<std::filesystem::path>::failure("Resource scheme is not mounted: " + path.scheme);
    const Root &root = mount->front();
    if (root.access != MountAccess::ReadWrite)
        return Result<std::filesystem::path>::failure("Resource scheme is read-only: " + path.scheme);
    return resolveIn(root, path);
}

Result<void> VirtualFileSystem::write(const VirtualPath &path, const Vector<uint8_t> &bytes,
                                      bool createDirectories) {
    const auto *mount = findMount(path.scheme);
    if (mount == nullptr || mount->empty())
        return Result<void>::failure("Resource scheme is not mounted: " + path.scheme);
    const Root &root = mount->front();
    if (root.access != MountAccess::ReadWrite)
        return Result<void>::failure("Resource scheme is read-only: " + path.scheme);
    auto target = resolveIn(root, path);
    if (!target)
        return Result<void>::failure(target.error);
    std::error_code error;
    if (createDirectories) {
        std::filesystem::create_directories(target.value.parent_path(), error);
        if (error)
            return Result<void>::failure("Cannot create the directory for " + path.toString() + ": " +
                                         toUrsaString(error.message()));
    }
    if (std::filesystem::is_directory(target.value, error) && !error)
        return Result<void>::failure("Resource path is a directory: " + path.toString());
    std::ofstream output(target.value, std::ios::binary | std::ios::trunc);
    if (!output)
        return Result<void>::failure("Cannot open " + path.toString() + " for writing");
    if (!bytes.empty())
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output)
        return Result<void>::failure("Cannot write " + path.toString());
    return {};
}

Result<Vector<VirtualFileEntry>> VirtualFileSystem::list(const String &scheme) const {
    const auto *mount = findMount(scheme);
    if (mount == nullptr)
        return Result<Vector<VirtualFileEntry>>::failure("Resource scheme is not mounted: " + scheme);

    Map<String, VirtualFileEntry> entries;
    for (const Root &root : *mount) {
        std::error_code error;
        std::filesystem::recursive_directory_iterator entry(root.path,
                                                            std::filesystem::directory_options::skip_permission_denied,
                                                            error);
        const std::filesystem::recursive_directory_iterator end;
        if (error)
            return Result<Vector<VirtualFileEntry>>::failure("Cannot scan mount " + pathToUrsaString(root.path) +
                                                             ": " + toUrsaString(error.message()));
        while (entry != end) {
            const auto path = entry->path();
            const bool link = entry->is_symlink(error);
            if (error)
                return Result<Vector<VirtualFileEntry>>::failure("Cannot inspect " + pathToUrsaString(path) + ": " +
                                                                 toUrsaString(error.message()));
            const bool directory = entry->is_directory(error);
            if (error)
                return Result<Vector<VirtualFileEntry>>::failure("Cannot inspect " + pathToUrsaString(path) + ": " +
                                                                 toUrsaString(error.message()));
            if (link && directory)
                entry.disable_recursion_pending();
            if (!link && directory) {
                entry.increment(error);
                if (error)
                    return Result<Vector<VirtualFileEntry>>::failure("Cannot continue the scan at " +
                                                                     pathToUrsaString(root.path) + ": " +
                                                                     toUrsaString(error.message()));
                continue;
            }
            const bool file = !link && entry->is_regular_file(error);
            if (error)
                return Result<Vector<VirtualFileEntry>>::failure("Cannot inspect " + pathToUrsaString(path) + ": " +
                                                                 toUrsaString(error.message()));
            if (file) {
                const auto canonical = std::filesystem::canonical(path, error);
                if (!error && containedIn(root.path, canonical)) {
                    const auto key = virtualRelative(root.path, canonical);
                    const auto found = entries.find(key);
                    if (found == entries.end())
                        entries.emplace(key, VirtualFileEntry{VirtualPath{scheme, key}, {}});
                    else
                        found->second.shadowedHostPaths.push_back(canonical);
                }
            }
            entry.increment(error);
            if (error)
                return Result<Vector<VirtualFileEntry>>::failure("Cannot continue the scan at " +
                                                                 pathToUrsaString(root.path) + ": " +
                                                                 toUrsaString(error.message()));
        }
    }

    Vector<VirtualFileEntry> result;
    result.reserve(entries.size());
    for (auto &entry : entries)
        result.push_back(std::move(entry.second));
    return Result<Vector<VirtualFileEntry>>::success(std::move(result));
}
} // namespace ursa
