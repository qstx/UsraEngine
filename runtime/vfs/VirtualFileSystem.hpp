#pragma once

#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <vfs/VirtualPath.hpp>
#include <cstdint>
#include <filesystem>

namespace Ursa
{
enum class MountAccess
{
	readOnly,
	readWrite
};

// One file reported by VirtualFileSystem::list(). The path resolves to the
// first root that contains it; shadowedHostPaths names host locations in later
// roots that the winning entry masks.
struct VirtualFileEntry
{
	VirtualPath path;
	Vector<std::filesystem::path> shadowedHostPaths;
};

// Addresses resources as "<scheme>:<relative-path>" over an ordered list of
// host directories. It owns path validation and access policy only; it does not
// know about resource kinds or identities.
class VirtualFileSystem final
{
public:
	Result<void> Mount(const String& scheme, const std::filesystem::path& root, MountAccess access);

	bool IsMounted(const String& scheme) const;

	Vector<String> Schemes() const;

	Result<Vector<std::filesystem::path>> Roots(const String& scheme) const;

	Result<Vector<uint8_t>> Read(const VirtualPath& path) const;

	Result<Vector<uint8_t>> ReadPrefix(const VirtualPath& path, uint64_t maxBytes) const;

	Result<String> ReadText(const VirtualPath& path) const;

	Result<bool> Exists(const VirtualPath& path) const;

	Result<uint64_t> Size(const VirtualPath& path) const;

	Result<std::filesystem::path> ResolveToDisk(const VirtualPath& path) const;

	// The host path a write would target. The first root of the scheme must be
	// writable; the file itself does not have to exist yet.
	Result<std::filesystem::path> ResolveForWrite(const VirtualPath& path) const;

	Result<void> Write(const VirtualPath& path, const Vector<uint8_t>& bytes, bool createDirectories = true);

	Result<Vector<VirtualFileEntry>> List(const String& scheme) const;

private:
	struct Root
	{
		std::filesystem::path path;
		MountAccess access;
	};

	const Vector<Root>* FindMount(const String& scheme) const;

	Result<std::filesystem::path> ResolveIn(const Root& root, const VirtualPath& path) const;

	Result<std::filesystem::path> ResolveExisting(const VirtualPath& path) const;

	Map<String, Vector<Root>> mounts;
};
} // namespace Ursa
