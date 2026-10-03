#pragma once

#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <vfs/VirtualPath.hpp>
#include <cstdint>
#include <filesystem>

namespace ursa {
enum class MountAccess { ReadOnly, ReadWrite };

// One file reported by VirtualFileSystem::list(). The path resolves to the
// first root that contains it; shadowedHostPaths names host locations in later
// roots that the winning entry masks.
struct VirtualFileEntry {
    VirtualPath path;
    Vector<std::filesystem::path> shadowedHostPaths;
};

// Addresses resources as "<scheme>:<relative-path>" over an ordered list of
// host directories. It owns path validation and access policy only; it does not
// know about resource kinds or identities.
class VirtualFileSystem final {
public:
    Result<void> mount(const String &scheme, const std::filesystem::path &root, MountAccess access);

    bool isMounted(const String &scheme) const;

    Vector<String> schemes() const;

    Result<Vector<std::filesystem::path>> roots(const String &scheme) const;

    Result<Vector<uint8_t>> read(const VirtualPath &path) const;

    Result<Vector<uint8_t>> readPrefix(const VirtualPath &path, uint64_t maxBytes) const;

    Result<String> readText(const VirtualPath &path) const;

    Result<bool> exists(const VirtualPath &path) const;

    Result<uint64_t> size(const VirtualPath &path) const;

    Result<std::filesystem::path> resolveToDisk(const VirtualPath &path) const;

    // The host path a write would target. The first root of the scheme must be
    // writable; the file itself does not have to exist yet.
    Result<std::filesystem::path> resolveForWrite(const VirtualPath &path) const;

    Result<void> write(const VirtualPath &path, const Vector<uint8_t> &bytes, bool createDirectories = true);

    Result<Vector<VirtualFileEntry>> list(const String &scheme) const;

private:
    struct Root {
        std::filesystem::path path;
        MountAccess access;
    };

    const Vector<Root> *findMount(const String &scheme) const;

    Result<std::filesystem::path> resolveIn(const Root &root, const VirtualPath &path) const;

    Result<std::filesystem::path> resolveExisting(const VirtualPath &path) const;

    Map<String, Vector<Root>> mounts_;
};
} // namespace ursa
