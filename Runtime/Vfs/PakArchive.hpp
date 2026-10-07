#pragma once

#include <Vfs/VirtualFileSystem.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <mutex>

namespace Ursa
{
inline constexpr UInt32 pakFormatVersion = 1;
inline constexpr UInt32 pakBlockBytes = 64 * 1024;

UInt32 PakChecksum(const UInt8* data, Size size);

class PakArchive final : public std::enable_shared_from_this<PakArchive>
{
public:
	static Result<std::shared_ptr<PakArchive>> Open(const std::filesystem::path& path);

	Result<std::shared_ptr<VirtualFileProvider>> View(const String& scheme);

	Result<void> Mount(VirtualFileSystem& filesystem);

	Vector<String> Schemes() const;

	UInt64 VerifiedBlockCount() const;

private:
	friend class PakFileProvider;

	struct Entry
	{
		VirtualPath path;
		UInt64 offset = 0;
		UInt64 size = 0;
		Vector<UInt32> checksums;
		mutable Set<Size> verified;
	};

	Result<Vector<UInt8>> Read(const String& key, UInt64 offset, UInt64 size) const;

	std::shared_ptr<DirectoryFileProvider> backing;
	String fileName;
	Map<String, Entry> entries;
	mutable std::mutex mutex;
	mutable UInt64 verifiedBlockCount = 0;
};
} // namespace Ursa
