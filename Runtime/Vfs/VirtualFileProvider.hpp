#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Core/Types.hpp>
#include <filesystem>
#include <memory>

namespace Ursa
{
class VirtualFileLease
{
public:
	virtual ~VirtualFileLease() = default;
};

// A storage provider implements file operations for a VFS mount without
// exposing its backing store to resource consumers.
class VirtualFileProvider
{
public:
	virtual ~VirtualFileProvider() = default;

	virtual String Identity() const = 0;

	virtual String SourceLabel() const = 0;

	virtual Result<bool> Exists(const String& relativePath) const = 0;

	virtual Result<Ursa::UInt64> Size(const String& relativePath) const = 0;

	virtual Result<Vector<Ursa::UInt8>> ReadRange(const String& relativePath, Ursa::UInt64 offset,
	                                              Ursa::UInt64 size) const = 0;

	virtual Result<Vector<String>> List() const = 0;

	virtual Result<void> WriteAtomic(const String& relativePath, const Vector<Ursa::UInt8>& bytes,
	                                 bool createDirectories) = 0;

	virtual Result<void> Append(const String& relativePath, const Vector<Ursa::UInt8>& bytes,
	                            bool createDirectories) = 0;

	virtual Result<void> CreateDirectory(const String&)
	{
		return Result<void>::Failure("Provider does not support directory creation");
	}

	virtual Result<void> RemoveDirectory(const String&, bool)
	{
		return Result<void>::Failure("Provider does not support directory removal");
	}

	virtual std::filesystem::path DirectoryRoot() const
	{
		return {};
	}

	virtual Result<void> Remove(const String&)
	{
		return Result<void>::Failure("Provider does not support removal");
	}

	virtual Result<void> CheckWritable(const String&) const
	{
		return {};
	}

	virtual Result<void> Rename(const String&, const String&)
	{
		return Result<void>::Failure("Provider does not support rename");
	}

	virtual Result<std::shared_ptr<VirtualFileLease>> AcquireExclusiveLease(const String&)
	{
		return Result<std::shared_ptr<VirtualFileLease>>::Failure("Provider does not support exclusive leases");
	}
};
} // namespace Ursa
