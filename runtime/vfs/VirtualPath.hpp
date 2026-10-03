#pragma once

#include <core/Containers.hpp>
#include <core/Result.hpp>

namespace ursa {
// Conventional scheme names. The file system does not require these names; they
// are the names the engine and editor entry points register.
inline constexpr const char *engineScheme = "ursa";
inline constexpr const char *projectScheme = "prj";

// A resource address of the form "<scheme>:<relative-path>", for example
// "ursa:shaders/renderer3d/cube.vert" or "prj:snake/snake.ursascene".
// The scheme names a mount; the relative path never escapes that mount.
struct VirtualPath {
    String scheme;
    String relative;

    static Result<VirtualPath> parse(const String &text);

    String toString() const;

    // The last path segment, used for browser labels and window titles.
    String fileName() const;
};

bool operator==(const VirtualPath &left, const VirtualPath &right);

bool operator!=(const VirtualPath &left, const VirtualPath &right);
} // namespace ursa
