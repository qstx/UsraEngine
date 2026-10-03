#pragma once

#include <serialization/ResourceIndex.hpp>
#include <filesystem>
#include <memory>

namespace ursa::editor {
// The scheme names are a convention this editor registers: project resources
// live under "<project>/assets", engine resources under the read-only mounts.
inline constexpr const char *projectResourceScheme = projectScheme;
inline constexpr const char *engineResourceScheme = engineScheme;

class ProjectWorkspace final {
public:
    static Result<std::unique_ptr<ProjectWorkspace>> open(const std::filesystem::path &projectDirectory,
                                                          const Vector<ResourceMount> &engineMounts = {});

    Result<void> refresh();

    // Host path a save would target. The virtual path must resolve inside the
    // writable project mount.
    Result<std::filesystem::path> writablePath(const VirtualPath &path) const;

    Vector<ResourceEntry> resources() const {
        return index_.entries();
    }

    const ResourceIndex &index() const {
        return index_;
    }

    const std::filesystem::path &projectRoot() const {
        return projectRoot_;
    }

    const std::filesystem::path &assetsRoot() const {
        return assetsRoot_;
    }

private:
    std::filesystem::path projectRoot_;
    std::filesystem::path assetsRoot_;
    Vector<ResourceMount> engineMounts_;
    ResourceIndex index_;
};
} // namespace ursa::editor
