#include <Core/Types.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>
#include <cwctype>
#include <fstream>

namespace Ursa
{
namespace
{
bool SamePart(const std::filesystem::path& left, const std::filesystem::path& right)
{
#ifdef _WIN32
	const auto a = left.wstring();
	const auto b = right.wstring();
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y)
	                                          { return std::towlower(x) == std::towlower(y); });
#else
	return left == right;
#endif
}

bool ContainedIn(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
	auto part = root.begin();
	auto entry = candidate.begin();
	for (; part != root.end(); ++part, ++entry)
	{
		if (entry == candidate.end() || !SamePart(*part, *entry))
		{
			return false;
		}
	}
	return true;
}

String VirtualRelative(const std::filesystem::path& root, const std::filesystem::path& path)
{
	return PathToUrsaString(path.lexically_relative(root).generic_u8string());
}
} // namespace

Result<void> VirtualFileSystem::Mount(const String& scheme, const std::filesystem::path& root, MountAccess access)
{
	if (scheme.empty())
	{
		return Result<void>::Failure("Resource scheme name is empty");
	}
	std::error_code error;
	const auto canonical = std::filesystem::canonical(root, error);
	if (error || !std::filesystem::is_directory(canonical))
	{
		return Result<void>::Failure("Resource mount is not a readable directory: " + PathToUrsaString(root));
	}
	auto& list = mounts[scheme];
	if (std::find_if(list.begin(), list.end(), [&](const Root& candidate) { return candidate.path == canonical; }) !=
	    list.end())
	{
		return {};
	}
	list.push_back(Root{canonical, access});
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
	const auto* mount = FindMount(scheme);
	if (mount == nullptr)
	{
		return Result<Vector<std::filesystem::path>>::Failure("Resource scheme is not mounted: " + scheme);
	}
	Vector<std::filesystem::path> result;
	result.reserve(mount->size());
	for (const Root& root : *mount)
	{
		result.push_back(root.path);
	}
	return Result<Vector<std::filesystem::path>>::Success(std::move(result));
}

const Vector<VirtualFileSystem::Root>* VirtualFileSystem::FindMount(const String& scheme) const
{
	const auto found = mounts.find(scheme);
	return found == mounts.end() ? nullptr : &found->second;
}

Result<std::filesystem::path> VirtualFileSystem::ResolveIn(const Root& root, const VirtualPath& path) const
{
	std::error_code error;
	const auto candidate = std::filesystem::weakly_canonical(root.path / PathFromUrsaString(path.relative), error);
	if (error)
	{
		return Result<std::filesystem::path>::Failure("Cannot resolve " + path.ToString() + ": " +
		                                              ToUrsaString(error.message()));
	}
	if (!ContainedIn(root.path, candidate))
	{
		return Result<std::filesystem::path>::Failure("Resource path escapes its mount root: " + path.ToString());
	}
	return Result<std::filesystem::path>::Success(candidate);
}

Result<std::filesystem::path> VirtualFileSystem::ResolveExisting(const VirtualPath& path) const
{
	const auto* mount = FindMount(path.scheme);
	if (mount == nullptr)
	{
		return Result<std::filesystem::path>::Failure("Resource scheme is not mounted: " + path.scheme);
	}
	for (const Root& root : *mount)
	{
		auto candidate = ResolveIn(root, path);
		if (!candidate)
		{
			continue;
		}
		std::error_code error;
		if (std::filesystem::is_regular_file(candidate.value, error) && !error)
		{
			return Result<std::filesystem::path>::Success(candidate.value);
		}
	}
	return Result<std::filesystem::path>::Failure("Resource not found: " + path.ToString());
}

Result<Vector<Ursa::UInt8>> VirtualFileSystem::Read(const VirtualPath& path) const
{
	auto resolved = ResolveExisting(path);
	if (!resolved)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(resolved.error);
	}
	std::ifstream input(resolved.value, std::ios::binary | std::ios::ate);
	if (!input)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Cannot open " + path.ToString());
	}
	const auto size = input.tellg();
	if (size < 0)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Cannot size " + path.ToString());
	}
	Vector<Ursa::UInt8> bytes(static_cast<Ursa::Size>(size));
	input.seekg(0);
	if (!bytes.empty())
	{
		input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	if (!input)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Cannot read " + path.ToString());
	}
	return Result<Vector<Ursa::UInt8>>::Success(std::move(bytes));
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

Result<Vector<Ursa::UInt8>> VirtualFileSystem::ReadPrefix(const VirtualPath& path, Ursa::UInt64 maxBytes) const
{
	auto resolved = ResolveExisting(path);
	if (!resolved)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(resolved.error);
	}
	std::ifstream input(resolved.value, std::ios::binary | std::ios::ate);
	if (!input)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Cannot open " + path.ToString());
	}
	const auto size = input.tellg();
	if (size < 0)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Cannot size " + path.ToString());
	}
	const Ursa::UInt64 available = static_cast<Ursa::UInt64>(size);
	const Ursa::UInt64 wanted = available < maxBytes ? available : maxBytes;
	Vector<Ursa::UInt8> bytes(static_cast<Ursa::Size>(wanted));
	input.seekg(0);
	if (!bytes.empty())
	{
		input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	if (!input)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Cannot read " + path.ToString());
	}
	return Result<Vector<Ursa::UInt8>>::Success(std::move(bytes));
}

Result<bool> VirtualFileSystem::Exists(const VirtualPath& path) const
{
	const auto* mount = FindMount(path.scheme);
	if (mount == nullptr)
	{
		return Result<bool>::Failure("Resource scheme is not mounted: " + path.scheme);
	}
	for (const Root& root : *mount)
	{
		auto candidate = ResolveIn(root, path);
		if (!candidate)
		{
			continue;
		}
		std::error_code error;
		const bool file = std::filesystem::is_regular_file(candidate.value, error) && !error;
		if (file)
		{
			return Result<bool>::Success(true);
		}
	}
	return Result<bool>::Success(false);
}

Result<Ursa::UInt64> VirtualFileSystem::Size(const VirtualPath& path) const
{
	auto resolved = ResolveExisting(path);
	if (!resolved)
	{
		return Result<Ursa::UInt64>::Failure(resolved.error);
	}
	std::error_code error;
	const auto bytes = std::filesystem::file_size(resolved.value, error);
	if (error)
	{
		return Result<Ursa::UInt64>::Failure("Cannot read the size of " + path.ToString() + ": " +
		                                     ToUrsaString(error.message()));
	}
	return Result<Ursa::UInt64>::Success(static_cast<Ursa::UInt64>(bytes));
}

Result<std::filesystem::path> VirtualFileSystem::ResolveToDisk(const VirtualPath& path) const
{
	return ResolveExisting(path);
}

Result<std::filesystem::path> VirtualFileSystem::ResolveForWrite(const VirtualPath& path) const
{
	const auto* mount = FindMount(path.scheme);
	if (mount == nullptr || mount->empty())
	{
		return Result<std::filesystem::path>::Failure("Resource scheme is not mounted: " + path.scheme);
	}
	const Root& root = mount->front();
	if (root.access != MountAccess::ReadWrite)
	{
		return Result<std::filesystem::path>::Failure("Resource scheme is read-only: " + path.scheme);
	}
	return ResolveIn(root, path);
}

Result<void> VirtualFileSystem::Write(const VirtualPath& path, const Vector<Ursa::UInt8>& bytes, bool createDirectories)
{
	const auto* mount = FindMount(path.scheme);
	if (mount == nullptr || mount->empty())
	{
		return Result<void>::Failure("Resource scheme is not mounted: " + path.scheme);
	}
	const Root& root = mount->front();
	if (root.access != MountAccess::ReadWrite)
	{
		return Result<void>::Failure("Resource scheme is read-only: " + path.scheme);
	}
	auto target = ResolveIn(root, path);
	if (!target)
	{
		return Result<void>::Failure(target.error);
	}
	std::error_code error;
	if (createDirectories)
	{
		std::filesystem::create_directories(target.value.parent_path(), error);
		if (error)
		{
			return Result<void>::Failure("Cannot create the directory for " + path.ToString() + ": " +
			                             ToUrsaString(error.message()));
		}
	}
	if (std::filesystem::is_directory(target.value, error) && !error)
	{
		return Result<void>::Failure("Resource path is a directory: " + path.ToString());
	}
	std::ofstream output(target.value, std::ios::binary | std::ios::trunc);
	if (!output)
	{
		return Result<void>::Failure("Cannot open " + path.ToString() + " for writing");
	}
	if (!bytes.empty())
	{
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	if (!output)
	{
		return Result<void>::Failure("Cannot write " + path.ToString());
	}
	return {};
}

Result<Vector<VirtualFileEntry>> VirtualFileSystem::List(const String& scheme) const
{
	const auto* mount = FindMount(scheme);
	if (mount == nullptr)
	{
		return Result<Vector<VirtualFileEntry>>::Failure("Resource scheme is not mounted: " + scheme);
	}

	Map<String, VirtualFileEntry> entries;
	for (const Root& root : *mount)
	{
		std::error_code error;
		std::filesystem::recursive_directory_iterator entry(
		    root.path, std::filesystem::directory_options::skip_permission_denied, error);
		const std::filesystem::recursive_directory_iterator end;
		if (error)
		{
			return Result<Vector<VirtualFileEntry>>::Failure("Cannot scan mount " + PathToUrsaString(root.path) + ": " +
			                                                 ToUrsaString(error.message()));
		}
		while (entry != end)
		{
			const auto path = entry->path();
			const bool link = entry->is_symlink(error);
			if (error)
			{
				return Result<Vector<VirtualFileEntry>>::Failure("Cannot inspect " + PathToUrsaString(path) + ": " +
				                                                 ToUrsaString(error.message()));
			}
			const bool directory = entry->is_directory(error);
			if (error)
			{
				return Result<Vector<VirtualFileEntry>>::Failure("Cannot inspect " + PathToUrsaString(path) + ": " +
				                                                 ToUrsaString(error.message()));
			}
			if (link && directory)
			{
				entry.disable_recursion_pending();
			}
			if (!link && directory)
			{
				entry.increment(error);
				if (error)
				{
					return Result<Vector<VirtualFileEntry>>::Failure("Cannot continue the scan at " +
					                                                 PathToUrsaString(root.path) + ": " +
					                                                 ToUrsaString(error.message()));
				}
				continue;
			}
			const bool file = !link && entry->is_regular_file(error);
			if (error)
			{
				return Result<Vector<VirtualFileEntry>>::Failure("Cannot inspect " + PathToUrsaString(path) + ": " +
				                                                 ToUrsaString(error.message()));
			}
			if (file)
			{
				const auto canonical = std::filesystem::canonical(path, error);
				if (!error && ContainedIn(root.path, canonical))
				{
					const auto key = VirtualRelative(root.path, canonical);
					const auto found = entries.find(key);
					if (found == entries.end())
					{
						entries.emplace(key, VirtualFileEntry{VirtualPath{scheme, key}, {}});
					}
					else
					{
						found->second.shadowedHostPaths.push_back(canonical);
					}
				}
			}
			entry.increment(error);
			if (error)
			{
				return Result<Vector<VirtualFileEntry>>::Failure("Cannot continue the scan at " +
				                                                 PathToUrsaString(root.path) + ": " +
				                                                 ToUrsaString(error.message()));
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
} // namespace Ursa
