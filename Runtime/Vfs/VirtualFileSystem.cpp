#include <Core/Types.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <algorithm>

namespace Ursa
{
Result<void> VirtualFileSystem::Mount(const String& scheme, const std::filesystem::path& root, MountAccess access)
{
	auto provider = DirectoryFileProvider::Open(root);
	if (!provider)
	{
		return Result<void>::Failure(provider.error);
	}
	return MountProvider(scheme, std::move(provider.value), access);
}

Result<void> VirtualFileSystem::MountProvider(const String& scheme, std::shared_ptr<VirtualFileProvider> provider,
                                              MountAccess access)
{
	if (scheme.empty())
	{
		return Result<void>::Failure("Resource scheme name is empty");
	}
	if (!provider)
	{
		return Result<void>::Failure("Cannot mount an empty VFS provider");
	}
	auto& mounted = mounts[scheme];
	const String identity = provider->Identity();
	const bool alreadyMounted = std::any_of(mounted.begin(), mounted.end(), [&](const MountedProvider& entry)
	                                        { return entry.provider->Identity() == identity; });
	if (alreadyMounted)
	{
		return {};
	}
	mounted.push_back({std::move(provider), access});
	return {};
}

bool VirtualFileSystem::IsMounted(const String& scheme) const
{
	return FindMount(scheme) != nullptr;
}

Vector<String> VirtualFileSystem::Schemes() const
{
	Vector<String> result;
	result.reserve(mounts.size());
	for (const auto& entry : mounts)
	{
		result.push_back(entry.first);
	}
	return result;
}

Result<Vector<std::filesystem::path>> VirtualFileSystem::Roots(const String& scheme) const
{
	const auto* mounted = FindMount(scheme);
	if (mounted == nullptr)
	{
		return Result<Vector<std::filesystem::path>>::Failure("Resource scheme is not mounted: " + scheme);
	}
	Vector<std::filesystem::path> result;
	for (const MountedProvider& entry : *mounted)
	{
		const auto root = entry.provider->DirectoryRoot();
		if (!root.empty())
		{
			result.push_back(root);
		}
	}
	return Result<Vector<std::filesystem::path>>::Success(std::move(result));
}

const Vector<VirtualFileSystem::MountedProvider>* VirtualFileSystem::FindMount(const String& scheme) const
{
	const auto found = mounts.find(scheme);
	return found == mounts.end() ? nullptr : &found->second;
}

Result<const VirtualFileSystem::MountedProvider*> VirtualFileSystem::ResolveExisting(const VirtualPath& path) const
{
	const auto* mounted = FindMount(path.scheme);
	if (mounted == nullptr)
	{
		return Result<const MountedProvider*>::Failure("Resource scheme is not mounted: " + path.scheme);
	}
	for (const MountedProvider& entry : *mounted)
	{
		auto exists = entry.provider->Exists(path.relative);
		if (!exists)
		{
			return Result<const MountedProvider*>::Failure(exists.error + " at " + path.ToString());
		}
		if (exists.value)
		{
			return Result<const MountedProvider*>::Success(&entry);
		}
	}
	return Result<const MountedProvider*>::Failure("Resource not found: " + path.ToString());
}

Result<const VirtualFileSystem::MountedProvider*> VirtualFileSystem::ResolveWriteProvider(const VirtualPath& path) const
{
	const auto* mounted = FindMount(path.scheme);
	if (mounted == nullptr || mounted->empty())
	{
		return Result<const MountedProvider*>::Failure("Resource scheme is not mounted: " + path.scheme);
	}
	const MountedProvider& entry = mounted->front();
	if (entry.access != MountAccess::ReadWrite)
	{
		return Result<const MountedProvider*>::Failure("Resource scheme is read-only: " + path.scheme);
	}
	return Result<const MountedProvider*>::Success(&entry);
}

Result<Vector<Ursa::UInt8>> VirtualFileSystem::Read(const VirtualPath& path) const
{
	const auto entry = ResolveExisting(path);
	if (!entry)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(entry.error);
	}
	auto size = entry.value->provider->Size(path.relative);
	if (!size)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(size.error + " at " + path.ToString());
	}
	return entry.value->provider->ReadRange(path.relative, 0, size.value);
}

Result<Vector<Ursa::UInt8>> VirtualFileSystem::ReadPrefix(const VirtualPath& path, Ursa::UInt64 maxBytes) const
{
	auto size = Size(path);
	if (!size)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(size.error);
	}
	const Ursa::UInt64 count = std::min(size.value, maxBytes);
	return ReadRange(path, 0, count);
}

Result<Vector<Ursa::UInt8>> VirtualFileSystem::ReadRange(const VirtualPath& path, Ursa::UInt64 offset,
                                                         Ursa::UInt64 size) const
{
	const auto entry = ResolveExisting(path);
	if (!entry)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(entry.error);
	}
	auto fileSize = entry.value->provider->Size(path.relative);
	if (!fileSize)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(fileSize.error + " at " + path.ToString());
	}
	if (offset > fileSize.value || size > fileSize.value - offset)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Requested byte range is outside the resource: " + path.ToString());
	}
	auto bytes = entry.value->provider->ReadRange(path.relative, offset, size);
	if (!bytes)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(bytes.error + " at " + path.ToString());
	}
	if (bytes.value.size() != size)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Provider returned a short range for " + path.ToString());
	}
	return bytes;
}

Result<String> VirtualFileSystem::ReadText(const VirtualPath& path) const
{
	auto bytes = Read(path);
	if (!bytes)
	{
		return Result<String>::Failure(bytes.error);
	}
	return Result<String>::Success(String(reinterpret_cast<const char*>(bytes.value.data()), bytes.value.size()));
}

Result<bool> VirtualFileSystem::Exists(const VirtualPath& path) const
{
	const auto* mounted = FindMount(path.scheme);
	if (mounted == nullptr)
	{
		return Result<bool>::Failure("Resource scheme is not mounted: " + path.scheme);
	}
	for (const MountedProvider& entry : *mounted)
	{
		auto exists = entry.provider->Exists(path.relative);
		if (!exists)
		{
			return Result<bool>::Failure(exists.error + " at " + path.ToString());
		}
		if (exists.value)
		{
			return Result<bool>::Success(true);
		}
	}
	return Result<bool>::Success(false);
}

Result<Ursa::UInt64> VirtualFileSystem::Size(const VirtualPath& path) const
{
	const auto entry = ResolveExisting(path);
	if (!entry)
	{
		return Result<Ursa::UInt64>::Failure(entry.error);
	}
	auto size = entry.value->provider->Size(path.relative);
	return size ? size : Result<Ursa::UInt64>::Failure(size.error + " at " + path.ToString());
}

Result<void> VirtualFileSystem::Write(const VirtualPath& path, const Vector<Ursa::UInt8>& bytes, bool createDirectories)
{
	return WriteAtomic(path, bytes, createDirectories);
}

Result<void> VirtualFileSystem::WriteAtomic(const VirtualPath& path, const Vector<Ursa::UInt8>& bytes,
                                            bool createDirectories)
{
	const auto entry = ResolveWriteProvider(path);
	if (!entry)
	{
		return Result<void>::Failure(entry.error);
	}
	return entry.value->provider->WriteAtomic(path.relative, bytes, createDirectories);
}

Result<void> VirtualFileSystem::Append(const VirtualPath& path, const Vector<Ursa::UInt8>& bytes,
                                       bool createDirectories)
{
	const auto entry = ResolveWriteProvider(path);
	if (!entry)
	{
		return Result<void>::Failure(entry.error);
	}
	return entry.value->provider->Append(path.relative, bytes, createDirectories);
}

Result<void> VirtualFileSystem::CreateDirectory(const VirtualPath& path)
{
	const auto entry = ResolveWriteProvider(path);
	if (!entry)
	{
		return Result<void>::Failure(entry.error);
	}
	return entry.value->provider->CreateDirectory(path.relative);
}

Result<void> VirtualFileSystem::RemoveDirectory(const VirtualPath& path, bool removeEmptyChildren)
{
	const auto entry = ResolveWriteProvider(path);
	if (!entry)
	{
		return Result<void>::Failure(entry.error);
	}
	return entry.value->provider->RemoveDirectory(path.relative, removeEmptyChildren);
}

Result<Vector<VirtualFileEntry>> VirtualFileSystem::List(const String& scheme) const
{
	const auto* mounted = FindMount(scheme);
	if (mounted == nullptr)
	{
		return Result<Vector<VirtualFileEntry>>::Failure("Resource scheme is not mounted: " + scheme);
	}
	Map<String, VirtualFileEntry> entries;
	for (const MountedProvider& source : *mounted)
	{
		auto paths = source.provider->List();
		if (!paths)
		{
			return Result<Vector<VirtualFileEntry>>::Failure(paths.error + " in scheme " + scheme);
		}
		for (const String& relativePath : paths.value)
		{
			auto parsed = VirtualPath::Parse(scheme + ":" + relativePath);
			if (!parsed || parsed.value.relative != relativePath)
			{
				return Result<Vector<VirtualFileEntry>>::Failure("Provider returned an invalid virtual path: " +
				                                                 relativePath);
			}
			auto found = entries.find(relativePath);
			if (found == entries.end())
			{
				entries.emplace(relativePath, VirtualFileEntry{std::move(parsed.value), {}});
			}
			else
			{
				found->second.shadowedSources.push_back(source.provider->SourceLabel());
			}
		}
	}
	Vector<VirtualFileEntry> result;
	result.reserve(entries.size());
	for (auto& entry : entries)
	{
		result.push_back(std::move(entry.second));
	}
	return Result<Vector<VirtualFileEntry>>::Success(std::move(result));
}

Result<void> VirtualFileSystem::CheckWritable(const VirtualPath& path) const
{
	auto target = ResolveWriteProvider(path);
	return target ? target.value->provider->CheckWritable(path.relative) : Result<void>::Failure(target.error);
}

Result<std::filesystem::path> VirtualFileSystem::ResolveForNativeHostInterop(const VirtualPath& path) const
{
	if (path.scheme != userDataScheme)
	{
		return Result<std::filesystem::path>::Failure(
		    "Native host paths are restricted to files in the writable user: scheme");
	}
	auto writable = ResolveWriteProvider(path);
	if (!writable)
	{
		return Result<std::filesystem::path>::Failure(writable.error);
	}
	auto existing = ResolveExisting(path);
	if (!existing)
	{
		return Result<std::filesystem::path>::Failure(existing.error);
	}
	if (existing.value != writable.value)
	{
		return Result<std::filesystem::path>::Failure(
		    "Native host path does not resolve to the writable user: provider");
	}
	auto directory = std::dynamic_pointer_cast<DirectoryFileProvider>(existing.value->provider);
	if (!directory)
	{
		return Result<std::filesystem::path>::Failure("Native host path requires a directory-backed user: provider");
	}
	return directory->ResolveToDisk(path.relative);
}

Result<std::filesystem::path> VirtualFileSystem::ResolveForExternalToolInterop(const VirtualPath& path) const
{
	auto provider = ResolveExisting(path);
	if (!provider)
	{
		return Result<std::filesystem::path>::Failure(provider.error);
	}
	auto directory = std::dynamic_pointer_cast<DirectoryFileProvider>(provider.value->provider);
	if (!directory)
	{
		return Result<std::filesystem::path>::Failure("External tools require a directory-mounted file: " +
		                                              path.ToString());
	}
	return directory->ResolveToDisk(path.relative);
}

Vector<VirtualMount> VirtualFileSystem::Providers() const
{
	Vector<VirtualMount> providers;
	for (const auto& scheme : mounts)
	{
		for (const auto& mount : scheme.second)
		{
			providers.push_back({scheme.first, mount.provider, mount.access});
		}
	}
	return providers;
}

Result<void> VirtualFileSystem::Remove(const VirtualPath& path)
{
	auto target = ResolveWriteProvider(path);
	return target ? target.value->provider->Remove(path.relative) : Result<void>::Failure(target.error);
}

Result<void> VirtualFileSystem::Rename(const VirtualPath& source, const VirtualPath& destination)
{
	if (source.scheme != destination.scheme)
	{
		return Result<void>::Failure("Rename requires the same VFS scheme");
	}
	auto from = ResolveWriteProvider(source);
	auto to = ResolveWriteProvider(destination);
	if (!from || !to)
	{
		return Result<void>::Failure(!from ? from.error : to.error);
	}
	if (from.value->provider != to.value->provider)
	{
		return Result<void>::Failure("Rename requires the same provider");
	}
	return from.value->provider->Rename(source.relative, destination.relative);
}

Result<std::shared_ptr<VirtualFileLease>> VirtualFileSystem::AcquireExclusiveLease(const VirtualPath& path)
{
	auto provider = ResolveWriteProvider(path);
	return provider ? provider.value->provider->AcquireExclusiveLease(path.relative)
	                : Result<std::shared_ptr<VirtualFileLease>>::Failure(provider.error);
}
} // namespace Ursa
