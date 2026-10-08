#pragma once

#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Vfs/VirtualFileProvider.hpp>
#include <Vfs/VirtualPath.hpp>
#include <cstdint>
#include <filesystem>

namespace Ursa
{
enum class MountAccess
{
	ReadOnly,
	ReadWrite
};

// One file reported by VirtualFileSystem::List(). The path resolves to the
// first provider that contains it; shadowedSources names later providers that
// contain the same virtual path.
struct VirtualFileEntry
{
	VirtualPath path;
	Vector<String> shadowedSources;
};

struct VirtualMount
{
	String scheme;
	std::shared_ptr<VirtualFileProvider> provider;
	MountAccess access;
};

// Addresses resources as "<scheme>:<relative-path>" over ordered storage
// providers. The VFS owns path validation and access policy, not resource kinds
// or resource identities.
class VirtualFileSystem final
{
public:
	Result<void> Mount(const String& scheme, const std::filesystem::path& root, MountAccess access);

	Result<void> MountProvider(const String& scheme, std::shared_ptr<VirtualFileProvider> provider, MountAccess access);

	bool IsMounted(const String& scheme) const;

	Vector<String> Schemes() const;

	Vector<VirtualMount> Providers() const;

	Result<Vector<std::filesystem::path>> Roots(const String& scheme) const;

	Result<Vector<Ursa::UInt8>> Read(const VirtualPath& path) const;

	Result<Vector<Ursa::UInt8>> ReadPrefix(const VirtualPath& path, Ursa::UInt64 maxBytes) const;

	Result<Vector<Ursa::UInt8>> ReadRange(const VirtualPath& path, Ursa::UInt64 offset, Ursa::UInt64 size) const;

	Result<String> ReadText(const VirtualPath& path) const;

	Result<bool> Exists(const VirtualPath& path) const;

	Result<Ursa::UInt64> Size(const VirtualPath& path) const;

	Result<void> Write(const VirtualPath& path, const Vector<Ursa::UInt8>& bytes, bool createDirectories = true);

	Result<void> WriteAtomic(const VirtualPath& path, const Vector<Ursa::UInt8>& bytes, bool createDirectories = true);

	Result<void> Append(const VirtualPath& path, const Vector<Ursa::UInt8>& bytes, bool createDirectories = true);

	Result<void> Remove(const VirtualPath& path);

	Result<void> Rename(const VirtualPath& source, const VirtualPath& destination);

	Result<void> CheckWritable(const VirtualPath& path) const;

	// Returns a validated filename only for native platform APIs that require a path.
	// The path must already exist in the writable user: directory provider.
	Result<std::filesystem::path> ResolveForNativeHostInterop(const VirtualPath& path) const;

	// Resolves an existing directory-mounted file for an explicitly configured external tool.
	Result<std::filesystem::path> ResolveForExternalToolInterop(const VirtualPath& path) const;

	Result<Vector<VirtualFileEntry>> List(const String& scheme) const;

private:
	struct MountedProvider
	{
		std::shared_ptr<VirtualFileProvider> provider;
		MountAccess access;
	};

	const Vector<MountedProvider>* FindMount(const String& scheme) const;

	Result<const MountedProvider*> ResolveExisting(const VirtualPath& path) const;

	Result<const MountedProvider*> ResolveWriteProvider(const VirtualPath& path) const;

	Map<String, Vector<MountedProvider>> mounts;
};
} // namespace Ursa
