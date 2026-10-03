#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <serialization/ResourceFile.hpp>
#include <vfs/VirtualFileSystem.hpp>
#include <filesystem>

namespace ursa {
// One configured resource root. A scheme may be mounted several times; lookup
// orders the roots the way they were mounted.
struct ResourceMount {
    String scheme;
    std::filesystem::path root;
    MountAccess access = MountAccess::ReadOnly;
};

struct ResourceLocation {
    ResourceKind kind = ResourceKind::Texture;
    VirtualPath path;
};

struct ResourceEntry {
    ResourceId id;
    ResourceLocation location;
};

// Maps stable resource IDs to virtual paths across the mounted resource roots.
class ResourceIndex final {
public:
    Result<void> mount(const ResourceMount &mount);

    Result<void> rebuild();

    Result<ResourceLocation> resolve(ResourceId id, const String &referencedBy = {}) const;

    Result<std::filesystem::path> locate(const ResourceLocation &location) const;

    Result<std::filesystem::path> writablePath(const VirtualPath &path) const;

    Vector<ResourceEntry> entries() const;

    const VirtualFileSystem &filesystem() const {
        return filesystem_;
    }

    const Vector<ResourceMount> &mounts() const {
        return mounts_;
    }

private:
    VirtualFileSystem filesystem_;
    Vector<ResourceMount> mounts_;
    Map<String, ResourceLocation> locations_;
};
} // namespace ursa
