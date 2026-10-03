#include <core/StringInterop.hpp>
#include <serialization/ResourceIndex.hpp>
#include <algorithm>

namespace ursa {
Result<void> ResourceIndex::mount(const ResourceMount &mount) {
    auto added = filesystem_.mount(mount.scheme, mount.root, mount.access);
    if (!added)
        return added;
    std::error_code error;
    const auto canonical = std::filesystem::canonical(mount.root, error);
    if (error)
        return Result<void>::failure("Resource root is not a readable directory: " + pathToUrsaString(mount.root));
    const bool known = std::any_of(mounts_.begin(), mounts_.end(), [&](const ResourceMount &candidate) {
        return candidate.scheme == mount.scheme && candidate.root == canonical;
    });
    if (!known)
        mounts_.push_back(ResourceMount{mount.scheme, canonical, mount.access});
    return {};
}

Result<void> ResourceIndex::rebuild() {
    Map<String, ResourceLocation> candidate;
    for (const String &scheme : filesystem_.schemes()) {
        auto listed = filesystem_.list(scheme);
        if (!listed)
            return Result<void>::failure(listed.error);
        for (const VirtualFileEntry &entry : listed.value) {
            auto kind = resourceKindForPath(entry.path);
            if (!kind)
                continue;
            auto header = readResourceHeader(filesystem_, entry.path);
            if (!header)
                return Result<void>::failure(header.error);
            if (header.value.kind != kind.value)
                return Result<void>::failure("Resource kind does not match its extension at " +
                                             entry.path.toString());
            const auto key = header.value.id.toString();
            const auto found = candidate.find(key);
            if (found != candidate.end())
                return Result<void>::failure("Duplicate resource ID " + key + " in " +
                                             found->second.path.toString() + " and " + entry.path.toString());
            candidate.emplace(key, ResourceLocation{kind.value, entry.path});
        }
    }
    locations_ = std::move(candidate);
    return {};
}

Result<ResourceLocation> ResourceIndex::resolve(ResourceId id, const String &referencedBy) const {
    const auto key = id.toString();
    const auto found = locations_.find(key);
    if (found == locations_.end())
        return Result<ResourceLocation>::failure("Missing resource ID " + key +
                                                 (referencedBy.empty() ? String{} : " referenced by " + referencedBy));
    return Result<ResourceLocation>::success(found->second);
}

Result<std::filesystem::path> ResourceIndex::locate(const ResourceLocation &location) const {
    auto resolved = filesystem_.resolveToDisk(location.path);
    if (!resolved)
        return Result<std::filesystem::path>::failure(resolved.error);
    return Result<std::filesystem::path>::success(std::move(resolved.value));
}

Result<std::filesystem::path> ResourceIndex::writablePath(const VirtualPath &path) const {
    return filesystem_.resolveForWrite(path);
}

Vector<ResourceEntry> ResourceIndex::entries() const {
    Vector<ResourceEntry> result;
    result.reserve(locations_.size());
    for (const auto &entry : locations_) {
        auto id = ResourceId::parse(entry.first);
        if (id)
            result.push_back({id.value, entry.second});
    }
    std::sort(result.begin(), result.end(), [](const ResourceEntry &left, const ResourceEntry &right) {
        return left.location.path.toString() < right.location.path.toString();
    });
    return result;
}
} // namespace ursa
