#include <Vfs/PakArchive.hpp>
#include <Vfs/PakFormat.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>
#include <stdexcept>

namespace Ursa
{
UInt32 PakChecksum(const UInt8* data, Size size)
{
	UInt32 crc = 0xffffffffu;
	for (Size index = 0; index < size; ++index)
	{
		crc ^= data[index];
		for (unsigned bit = 0; bit < 8; ++bit)
		{
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

class PakFileProvider final : public VirtualFileProvider
{
public:
	PakFileProvider(std::shared_ptr<PakArchive> archive, String scheme)
	    : archive(std::move(archive)), scheme(std::move(scheme))
	{
	}

	String Identity() const override
	{
		return archive->backing->Identity() + "/" + archive->fileName + ":" + scheme;
	}

	String SourceLabel() const override
	{
		return "pak:" + archive->fileName + "#" + scheme;
	}

	Result<bool> Exists(const String& path) const override
	{
		return Result<bool>::Success(archive->entries.find(scheme + ":" + path) != archive->entries.end());
	}

	Result<UInt64> Size(const String& path) const override
	{
		auto found = archive->entries.find(scheme + ":" + path);
		return found == archive->entries.end() ? Result<UInt64>::Failure("Pak entry not found: " + scheme + ":" + path)
		                                       : Result<UInt64>::Success(found->second.size);
	}

	Result<Vector<UInt8>> ReadRange(const String& path, UInt64 offset, UInt64 size) const override
	{
		return archive->Read(scheme + ":" + path, offset, size);
	}

	Result<Vector<String>> List() const override
	{
		Vector<String> paths;
		for (const auto& item : archive->entries)
		{
			if (item.second.path.scheme == scheme)
			{
				paths.push_back(item.second.path.relative);
			}
		}
		return Result<Vector<String>>::Success(std::move(paths));
	}

	Result<void> WriteAtomic(const String&, const Vector<UInt8>&, bool) override
	{
		return Result<void>::Failure("Pak mounts are read-only");
	}

	Result<void> Append(const String&, const Vector<UInt8>&, bool) override
	{
		return Result<void>::Failure("Pak mounts are read-only");
	}

private:
	std::shared_ptr<PakArchive> archive;
	String scheme;
};

Result<std::shared_ptr<PakArchive>> PakArchive::Open(const std::filesystem::path& path)
{
	const String location = PathToUrsaString(path);
	try
	{
		std::error_code error;
		const auto absolute = std::filesystem::absolute(path, error);
		if (error)
		{
			throw std::runtime_error("Cannot resolve Pak bootstrap path");
		}
		auto backing = DirectoryFileProvider::Open(absolute.parent_path());
		if (!backing)
		{
			throw std::runtime_error(ToStdString(backing.error));
		}
		const String fileName = PathToUrsaString(absolute.filename());
		auto fileSize = backing.value->Size(fileName);
		if (!fileSize || fileSize.value < PakFormat::headerBytes)
		{
			throw std::runtime_error("Truncated Pak header");
		}
		auto header = backing.value->ReadRange(fileName, 0, PakFormat::headerBytes);
		if (!header || !std::equal(std::begin(PakFormat::magic), std::end(PakFormat::magic), header.value.begin()))
		{
			throw std::runtime_error("Invalid Pak magic");
		}
		Size cursor = 8;
		const auto number = [](const Vector<UInt8>& bytes, Size& cursor, Size count)
		{
			auto value = PakFormat::Take(bytes, cursor, count);
			if (!value)
			{
				throw std::runtime_error(ToStdString(value.error));
			}
			return value.value;
		};
		const UInt64 version = number(header.value, cursor, 4);
		const UInt64 count = number(header.value, cursor, 4);
		const UInt64 tableOffset = number(header.value, cursor, 8);
		const UInt64 tableSize = number(header.value, cursor, 8);
		const UInt32 tableChecksum = static_cast<UInt32>(number(header.value, cursor, 4));
		const UInt64 blockBytes = number(header.value, cursor, 4);
		if (version != pakFormatVersion)
		{
			throw std::runtime_error("Unsupported Pak version " + std::to_string(version));
		}
		if (count > PakFormat::maxEntries || blockBytes != pakBlockBytes || tableSize > PakFormat::maxTableBytes ||
		    tableOffset < PakFormat::headerBytes || tableOffset > fileSize.value ||
		    tableSize != fileSize.value - tableOffset)
		{
			throw std::runtime_error("Invalid Pak directory bounds or block size");
		}
		auto table = backing.value->ReadRange(fileName, tableOffset, tableSize);
		if (!table || PakChecksum(table.value.data(), table.value.size()) != tableChecksum)
		{
			throw std::runtime_error("Pak directory checksum failed");
		}
		auto archive = std::shared_ptr<PakArchive>(new PakArchive);
		archive->backing = backing.value;
		archive->fileName = fileName;
		cursor = 0;
		UInt64 payloadEnd = PakFormat::headerBytes;
		String previous;
		for (UInt64 index = 0; index < count; ++index)
		{
			const UInt64 nameBytes = number(table.value, cursor, 4);
			const UInt64 codec = number(table.value, cursor, 4);
			Entry entry;
			entry.offset = number(table.value, cursor, 8);
			entry.size = number(table.value, cursor, 8);
			const UInt64 blocks = number(table.value, cursor, 4);
			const UInt64 reserved = number(table.value, cursor, 4);
			const UInt64 expectedBlocks = entry.size / pakBlockBytes + (entry.size % pakBlockBytes != 0);
			if (codec != 0 || reserved != 0 || entry.offset != payloadEnd || entry.offset > tableOffset ||
			    entry.size > tableOffset - entry.offset || blocks != expectedBlocks ||
			    blocks > (table.value.size() - cursor) / 4)
			{
				throw std::runtime_error("Invalid Pak entry range, codec or checksum block count");
			}
			for (UInt64 block = 0; block < blocks; ++block)
			{
				entry.checksums.push_back(static_cast<UInt32>(number(table.value, cursor, 4)));
			}
			if (!nameBytes || nameBytes > table.value.size() - cursor)
			{
				throw std::runtime_error("Invalid Pak entry path length");
			}
			const String key(reinterpret_cast<const char*>(table.value.data() + cursor), static_cast<Size>(nameBytes));
			cursor += static_cast<Size>(nameBytes);
			auto parsed = VirtualPath::Parse(key);
			if (!parsed || parsed.value.ToString() != key || (!previous.empty() && key <= previous))
			{
				throw std::runtime_error("Invalid, duplicate or unsorted Pak virtual path: " + ToStdString(key));
			}
			entry.path = parsed.value;
			archive->entries.emplace(key, std::move(entry));
			payloadEnd += archive->entries.at(key).size;
			previous = key;
		}
		if (cursor != table.value.size() || payloadEnd != tableOffset)
		{
			throw std::runtime_error("Pak directory has trailing bytes or unindexed payload");
		}
		return Result<std::shared_ptr<PakArchive>>::Success(std::move(archive));
	}
	catch (const std::exception& error)
	{
		return Result<std::shared_ptr<PakArchive>>::Failure("Cannot mount Pak " + location + ": " + error.what());
	}
}

Result<std::shared_ptr<VirtualFileProvider>> PakArchive::View(const String& scheme)
{
	const auto schemes = Schemes();
	if (std::find(schemes.begin(), schemes.end(), scheme) == schemes.end())
	{
		return Result<std::shared_ptr<VirtualFileProvider>>::Failure("Pak has no scheme " + scheme);
	}
	return Result<std::shared_ptr<VirtualFileProvider>>::Success(
	    std::make_shared<PakFileProvider>(shared_from_this(), scheme));
}

Vector<String> PakArchive::Schemes() const
{
	Set<String> unique;
	for (const auto& item : entries)
	{
		unique.insert(item.second.path.scheme);
	}
	return Vector<String>(unique.begin(), unique.end());
}

Result<void> PakArchive::Mount(VirtualFileSystem& filesystem)
{
	for (const String& scheme : Schemes())
	{
		auto view = View(scheme);
		if (!view)
		{
			return Result<void>::Failure(view.error);
		}
		auto mounted = filesystem.MountProvider(scheme, view.value, MountAccess::ReadOnly);
		if (!mounted)
		{
			return mounted;
		}
	}
	return {};
}

UInt64 PakArchive::VerifiedBlockCount() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return verifiedBlockCount;
}

Result<Vector<UInt8>> PakArchive::Read(const String& key, UInt64 offset, UInt64 size) const
{
	std::lock_guard<std::mutex> lock(mutex);
	auto found = entries.find(key);
	if (found == entries.end() || offset > found->second.size || size > found->second.size - offset)
	{
		return Result<Vector<UInt8>>::Failure("Pak range is outside entry " + key);
	}
	const Entry& entry = found->second;
	Vector<UInt8> result;
	result.reserve(static_cast<Size>(size));
	UInt64 cursor = offset;
	while (cursor < offset + size)
	{
		const Size block = static_cast<Size>(cursor / pakBlockBytes);
		const UInt64 blockOffset = static_cast<UInt64>(block) * pakBlockBytes;
		const UInt64 blockSize = std::min<UInt64>(pakBlockBytes, entry.size - blockOffset);
		auto bytes = backing->ReadRange(fileName, entry.offset + blockOffset, blockSize);
		if (!bytes)
		{
			return Result<Vector<UInt8>>::Failure(bytes.error + " in Pak " + fileName + " entry " + key);
		}
		if (!entry.verified.count(block))
		{
			if (PakChecksum(bytes.value.data(), bytes.value.size()) != entry.checksums[block])
			{
				return Result<Vector<UInt8>>::Failure("Pak block checksum failed in " + fileName + " entry " + key);
			}
			entry.verified.insert(block);
			++verifiedBlockCount;
		}
		const UInt64 start = cursor - blockOffset;
		const UInt64 count = std::min<UInt64>(blockSize - start, offset + size - cursor);
		result.insert(result.end(), bytes.value.begin() + static_cast<Size>(start),
		              bytes.value.begin() + static_cast<Size>(start + count));
		cursor += count;
	}
	return Result<Vector<UInt8>>::Success(std::move(result));
}
} // namespace Ursa
