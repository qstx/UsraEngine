#include <core/StringInterop.hpp>
#include <serialization/ResourceIndex.hpp>
#include <algorithm>

namespace Ursa
{
Result<void> ResourceIndex::Mount(const ResourceMount& mount)
{
	auto added = filesystem.Mount(mount.scheme, mount.root, mount.access);
	if (!added)
	{
		return added;
	}
	std::error_code error;
	const auto canonical = std::filesystem::canonical(mount.root, error);
	if (error)
	{
		return Result<void>::Failure("Resource root is not a readable directory: " + PathToUrsaString(mount.root));
	}
	const bool known = std::any_of(mounts.begin(), mounts.end(), [&](const ResourceMount& candidate)
	                               { return candidate.scheme == mount.scheme && candidate.root == canonical; });
	if (!known)
	{
		mounts.push_back(ResourceMount{mount.scheme, canonical, mount.access});
	}
	return {};
}

Result<void> ResourceIndex::Rebuild()
{
	Map<String, ResourceLocation> candidate;
	for (const String& scheme : filesystem.Schemes())
	{
		auto listed = filesystem.List(scheme);
		if (!listed)
		{
			return Result<void>::Failure(listed.error);
		}
		for (const VirtualFileEntry& entry : listed.value)
		{
			auto kind = ResourceKindForPath(entry.path);
			if (!kind)
			{
				continue;
			}
			auto header = ReadResourceHeader(filesystem, entry.path);
			if (!header)
			{
				return Result<void>::Failure(header.error);
			}
			if (header.value.kind != kind.value)
			{
				return Result<void>::Failure("Resource kind does not match its extension at " + entry.path.ToString());
			}
			const auto key = header.value.id.ToString();
			const auto found = candidate.find(key);
			if (found != candidate.end())
			{
				return Result<void>::Failure("Duplicate resource ID " + key + " in " + found->second.path.ToString() +
				                             " and " + entry.path.ToString());
			}
			candidate.emplace(key, ResourceLocation{kind.value, entry.path});
		}
	}
	locations = std::move(candidate);
	return {};
}

Result<ResourceLocation> ResourceIndex::Resolve(ResourceId id, const String& referencedBy) const
{
	const auto key = id.ToString();
	const auto found = locations.find(key);
	if (found == locations.end())
	{
		return Result<ResourceLocation>::Failure("Missing resource ID " + key +
		                                         (referencedBy.empty() ? String{} : " referenced by " + referencedBy));
	}
	return Result<ResourceLocation>::Success(found->second);
}

Result<std::filesystem::path> ResourceIndex::Locate(const ResourceLocation& location) const
{
	auto resolved = filesystem.ResolveToDisk(location.path);
	if (!resolved)
	{
		return Result<std::filesystem::path>::Failure(resolved.error);
	}
	return Result<std::filesystem::path>::Success(std::move(resolved.value));
}

Result<std::filesystem::path> ResourceIndex::WritablePath(const VirtualPath& path) const
{
	return filesystem.ResolveForWrite(path);
}

Vector<ResourceEntry> ResourceIndex::Entries() const
{
	Vector<ResourceEntry> result;
	result.reserve(locations.size());
	for (const auto& entry : locations)
	{
		auto id = ResourceId::Parse(entry.first);
		if (id)
		{
			result.push_back({id.value, entry.second});
		}
	}
	std::sort(result.begin(), result.end(), [](const ResourceEntry& left, const ResourceEntry& right)
	          { return left.location.path.ToString() < right.location.path.ToString(); });
	return result;
}
} // namespace Ursa
