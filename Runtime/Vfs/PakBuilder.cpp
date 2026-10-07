#include <Vfs/PakBuilder.hpp>
#include <Vfs/PakFormat.hpp>
#include <algorithm>
#include <limits>

namespace Ursa
{
Result<Vector<UInt8>> PakBuilder::Encode(Vector<PakEntryData> entries)
{
	if (entries.size() > PakFormat::maxEntries)
	{
		return Result<Vector<UInt8>>::Failure("Too many Pak entries");
	}
	std::sort(entries.begin(), entries.end(),
	          [](const PakEntryData& a, const PakEntryData& b) { return a.path.ToString() < b.path.ToString(); });
	Vector<UInt8> package(PakFormat::headerBytes, 0);
	std::copy(std::begin(PakFormat::magic), std::end(PakFormat::magic), package.begin());
	Vector<UInt8> table;
	String previous;
	for (const auto& entry : entries)
	{
		const String key = entry.path.ToString();
		auto parsed = VirtualPath::Parse(key);
		if (!parsed || parsed.value != entry.path || (!previous.empty() && previous == key))
		{
			return Result<Vector<UInt8>>::Failure("Invalid or duplicate Pak virtual path: " + key);
		}
		const UInt64 blocks = entry.bytes.size() / pakBlockBytes + (entry.bytes.size() % pakBlockBytes != 0);
		if (key.size() > PakFormat::maxTableBytes || blocks > PakFormat::maxTableBytes / 4)
		{
			return Result<Vector<UInt8>>::Failure("Pak entry directory is too large: " + key);
		}
		PakFormat::Append(table, key.size(), 4);
		PakFormat::Append(table, 0, 4);
		PakFormat::Append(table, package.size(), 8);
		PakFormat::Append(table, entry.bytes.size(), 8);
		PakFormat::Append(table, blocks, 4);
		PakFormat::Append(table, 0, 4);
		for (UInt64 block = 0; block < blocks; ++block)
		{
			const Size offset = static_cast<Size>(block * pakBlockBytes);
			const Size size = std::min<Size>(pakBlockBytes, entry.bytes.size() - offset);
			PakFormat::Append(table, PakChecksum(entry.bytes.data() + offset, size), 4);
		}
		table.insert(table.end(), key.begin(), key.end());
		if (table.size() > PakFormat::maxTableBytes ||
		    entry.bytes.size() > (std::numeric_limits<Size>::max)() - package.size())
		{
			return Result<Vector<UInt8>>::Failure("Pak size exceeds supported bounds");
		}
		package.insert(package.end(), entry.bytes.begin(), entry.bytes.end());
		previous = key;
	}
	PakFormat::Put(package, 8, pakFormatVersion, 4);
	PakFormat::Put(package, 12, entries.size(), 4);
	PakFormat::Put(package, 16, package.size(), 8);
	PakFormat::Put(package, 24, table.size(), 8);
	PakFormat::Put(package, 32, PakChecksum(table.data(), table.size()), 4);
	PakFormat::Put(package, 36, pakBlockBytes, 4);
	package.insert(package.end(), table.begin(), table.end());
	return Result<Vector<UInt8>>::Success(std::move(package));
}

Result<void> PakBuilder::Build(const VirtualFileSystem& source, const Vector<String>& schemes,
                               VirtualFileSystem& destination, const VirtualPath& packagePath)
{
	Vector<PakEntryData> entries;
	Set<String> requested;
	for (const String& scheme : schemes)
	{
		if (!requested.insert(scheme).second)
		{
			return Result<void>::Failure("Duplicate Pak source scheme: " + scheme);
		}
		auto listed = source.List(scheme);
		if (!listed)
		{
			return Result<void>::Failure(listed.error);
		}
		for (const auto& entry : listed.value)
		{
			if (!entry.shadowedSources.empty())
			{
				return Result<void>::Failure("Conflicting Pak source path " + entry.path.ToString() + " from " +
				                             entry.shadowedSources.front());
			}
			auto bytes = source.Read(entry.path);
			if (!bytes)
			{
				return Result<void>::Failure(bytes.error);
			}
			entries.push_back({entry.path, std::move(bytes.value)});
		}
	}
	auto package = Encode(std::move(entries));
	return package ? destination.WriteAtomic(packagePath, package.value) : Result<void>::Failure(package.error);
}
} // namespace Ursa
