#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>

namespace Ursa
{
// Conventional scheme names. The file system does not require these names; they
// are the names the engine and editor entry points register.
inline constexpr const char* engineScheme = "ursa";
inline constexpr const char* projectScheme = "prj";
inline constexpr const char* userDataScheme = "user";

// A resource address of the form "<scheme>:<relative-path>", for example
// "ursa:Shaders/Renderer3D/Cube.vert" or "prj:Snake/Snake.ursascene".
// The scheme names a mount; the relative path never escapes that mount.
struct VirtualPath
{
	String scheme;
	String relative;

	static Result<VirtualPath> Parse(const String& text);

	String ToString() const;

	// The last path segment, used for browser labels and window titles.
	String FileName() const;
};

bool operator==(const VirtualPath& left, const VirtualPath& right);

bool operator!=(const VirtualPath& left, const VirtualPath& right);
} // namespace Ursa
