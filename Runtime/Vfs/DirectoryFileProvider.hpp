#pragma once

#include <Vfs/VirtualFileProvider.hpp>

namespace Ursa
{
class VirtualFileSystem;

class DirectoryFileProvider final : public VirtualFileProvider
{
public:
	Result<void> Remove(const String& relativePath) override;

	Result<void> CheckWritable(const String& relativePath) const override;

	Result<void> Rename(const String& source, const String& destination) override;

	static Result<std::shared_ptr<DirectoryFileProvider>> Open(const std::filesystem::path& root,
	                                                           bool createIfMissing = false);

	String Identity() const override;

	String SourceLabel() const override;

	Result<bool> Exists(const String& relativePath) const override;

	Result<Ursa::UInt64> Size(const String& relativePath) const override;

	Result<Vector<Ursa::UInt8>> ReadRange(const String& relativePath, Ursa::UInt64 offset,
	                                      Ursa::UInt64 size) const override;

	Result<Vector<String>> List() const override;

	Result<void> WriteAtomic(const String& relativePath, const Vector<Ursa::UInt8>& bytes,
	                         bool createDirectories) override;

	Result<void> Append(const String& relativePath, const Vector<Ursa::UInt8>& bytes, bool createDirectories) override;

	Result<std::shared_ptr<VirtualFileLease>> AcquireExclusiveLease(const String& relativePath) override;

	Result<void> CreateDirectory(const String& relativePath) override;

	Result<void> RemoveDirectory(const String& relativePath, bool removeEmptyChildren) override;

	std::filesystem::path DirectoryRoot() const override
	{
		return root;
	}

private:
	friend class VirtualFileSystem;

	Result<std::filesystem::path> ResolveToDisk(const String& relativePath) const;

	Result<std::filesystem::path> ResolveForWrite(const String& relativePath) const;

	explicit DirectoryFileProvider(std::filesystem::path canonicalRoot) : root(std::move(canonicalRoot))
	{
	}

	Result<std::filesystem::path> Resolve(const String& relativePath) const;

	std::filesystem::path root;
};
} // namespace Ursa
