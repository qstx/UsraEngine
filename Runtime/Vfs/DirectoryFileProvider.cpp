#include <Core/StringInterop.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Vfs/VirtualPath.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace Ursa
{
namespace
{
std::atomic<Ursa::UInt64> temporaryId{0};

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

String RelativePath(const std::filesystem::path& root, const std::filesystem::path& path)
{
	return PathToUrsaString(path.lexically_relative(root).generic_u8string());
}

String NativeError(const std::error_code& error)
{
	return ToUrsaString(error.message());
}
} // namespace

Result<std::shared_ptr<DirectoryFileProvider>> DirectoryFileProvider::Open(const std::filesystem::path& root,
                                                                           bool createIfMissing)
{
	std::error_code error;
	if (createIfMissing)
	{
		std::filesystem::create_directories(root, error);
		if (error)
		{
			return Result<std::shared_ptr<DirectoryFileProvider>>::Failure("Cannot create directory mount: " +
			                                                               NativeError(error));
		}
	}
	const auto canonical = std::filesystem::canonical(root, error);
	if (error || !std::filesystem::is_directory(canonical))
	{
		return Result<std::shared_ptr<DirectoryFileProvider>>::Failure("Directory mount is not a readable directory: " +
		                                                               PathToUrsaString(root));
	}
	return Result<std::shared_ptr<DirectoryFileProvider>>::Success(
	    std::shared_ptr<DirectoryFileProvider>(new DirectoryFileProvider(canonical)));
}

String DirectoryFileProvider::Identity() const
{
	return String("directory:") + PathToUrsaString(root.generic_u8string());
}

String DirectoryFileProvider::SourceLabel() const
{
	return String("directory:") + PathToUrsaString(root.filename().generic_u8string());
}

Result<std::filesystem::path> DirectoryFileProvider::Resolve(const String& relativePath) const
{
	auto parsed = VirtualPath::Parse(String("vfs:") + relativePath);
	if (!parsed)
	{
		return Result<std::filesystem::path>::Failure(parsed.error);
	}
	std::error_code error;
	const auto candidate = std::filesystem::weakly_canonical(root / PathFromUrsaString(parsed.value.relative), error);
	if (error)
	{
		return Result<std::filesystem::path>::Failure("Cannot resolve path in directory mount: " + NativeError(error));
	}
	if (!ContainedIn(root, candidate))
	{
		return Result<std::filesystem::path>::Failure("Path escapes its directory mount: " + relativePath);
	}
	return Result<std::filesystem::path>::Success(candidate);
}

Result<bool> DirectoryFileProvider::Exists(const String& relativePath) const
{
	auto path = Resolve(relativePath);
	if (!path)
	{
		return Result<bool>::Failure(path.error);
	}
	std::error_code error;
	const bool exists = std::filesystem::is_regular_file(path.value, error);
	if (error)
	{
		if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
		{
			return Result<bool>::Success(false);
		}
		return Result<bool>::Failure("Cannot inspect file in directory mount: " + NativeError(error));
	}
	return Result<bool>::Success(exists);
}

Result<Ursa::UInt64> DirectoryFileProvider::Size(const String& relativePath) const
{
	auto path = Resolve(relativePath);
	if (!path)
	{
		return Result<Ursa::UInt64>::Failure(path.error);
	}
	std::error_code error;
	const auto size = std::filesystem::file_size(path.value, error);
	if (error)
	{
		return Result<Ursa::UInt64>::Failure("Cannot read file size in directory mount: " + NativeError(error));
	}
	return Result<Ursa::UInt64>::Success(static_cast<Ursa::UInt64>(size));
}

Result<Vector<Ursa::UInt8>> DirectoryFileProvider::ReadRange(const String& relativePath, Ursa::UInt64 offset,
                                                             Ursa::UInt64 size) const
{
	auto path = Resolve(relativePath);
	if (!path)
	{
		return Result<Vector<Ursa::UInt8>>::Failure(path.error);
	}
	std::error_code error;
	const auto fileSize = std::filesystem::file_size(path.value, error);
	if (error)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Cannot read file size in directory mount: " + NativeError(error));
	}
	if (offset > fileSize || size > fileSize - offset ||
	    size > static_cast<Ursa::UInt64>((std::numeric_limits<std::streamsize>::max)()) ||
	    offset > static_cast<Ursa::UInt64>((std::numeric_limits<std::streamoff>::max)()))
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Requested byte range is outside the file: " + relativePath);
	}
	std::ifstream input(path.value, std::ios::binary);
	if (!input)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Cannot open file in directory mount: " + relativePath);
	}
	input.seekg(static_cast<std::streamoff>(offset));
	Vector<Ursa::UInt8> bytes(static_cast<Ursa::Size>(size));
	if (!bytes.empty())
	{
		input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	if (!input)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Cannot read file range in directory mount: " + relativePath);
	}
	return Result<Vector<Ursa::UInt8>>::Success(std::move(bytes));
}

Result<Vector<String>> DirectoryFileProvider::List() const
{
	Vector<String> paths;
	std::error_code error;
	std::filesystem::recursive_directory_iterator entry(
	    root, std::filesystem::directory_options::skip_permission_denied, error);
	const std::filesystem::recursive_directory_iterator end;
	if (error)
	{
		return Result<Vector<String>>::Failure("Cannot scan directory mount: " + NativeError(error));
	}
	while (entry != end)
	{
		const auto path = entry->path();
		const bool link = entry->is_symlink(error);
		if (error)
		{
			return Result<Vector<String>>::Failure("Cannot inspect directory mount entry: " + NativeError(error));
		}
		const bool directory = entry->is_directory(error);
		if (error)
		{
			return Result<Vector<String>>::Failure("Cannot inspect directory mount entry: " + NativeError(error));
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
				return Result<Vector<String>>::Failure("Cannot continue directory mount scan: " + NativeError(error));
			}
			continue;
		}
		const bool file = !link && entry->is_regular_file(error);
		if (error)
		{
			return Result<Vector<String>>::Failure("Cannot inspect directory mount entry: " + NativeError(error));
		}
		if (file)
		{
			const auto canonical = std::filesystem::canonical(path, error);
			if (!error && ContainedIn(root, canonical))
			{
				paths.push_back(RelativePath(root, canonical));
			}
		}
		entry.increment(error);
		if (error)
		{
			return Result<Vector<String>>::Failure("Cannot continue directory mount scan: " + NativeError(error));
		}
	}
	std::sort(paths.begin(), paths.end());
	return Result<Vector<String>>::Success(std::move(paths));
}

Result<void> DirectoryFileProvider::WriteAtomic(const String& relativePath, const Vector<Ursa::UInt8>& bytes,
                                                bool createDirectories)
{
	auto target = ResolveForWrite(relativePath);
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
			return Result<void>::Failure("Cannot create directory for " + relativePath + ": " + NativeError(error));
		}
	}
	if (std::filesystem::is_directory(target.value, error) && !error)
	{
		return Result<void>::Failure("Resource path is a directory: " + relativePath);
	}
	const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto suffix =
	    std::to_string(timestamp) + "-" + std::to_string(temporaryId.fetch_add(1, std::memory_order_relaxed));
	const auto temporary =
	    target.value.parent_path() / (target.value.filename().u8string() + ".vfs-" + suffix + ".tmp");
	{
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output)
		{
			return Result<void>::Failure("Cannot create temporary VFS file for " + relativePath);
		}
		if (!bytes.empty())
		{
			output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}
		output.flush();
		if (!output)
		{
			output.close();
			std::filesystem::remove(temporary, error);
			return Result<void>::Failure("Cannot write temporary VFS file for " + relativePath);
		}
		output.close();
		if (!output)
		{
			std::filesystem::remove(temporary, error);
			return Result<void>::Failure("Cannot close temporary VFS file for " + relativePath);
		}
	}
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesW(target.value.c_str());
	bool replaced = false;
	if (attributes != INVALID_FILE_ATTRIBUTES)
	{
		replaced = ReplaceFileW(target.value.c_str(), temporary.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS,
		                        nullptr, nullptr) != 0;
	}
	else
	{
		const DWORD queryError = GetLastError();
		replaced = (queryError == ERROR_FILE_NOT_FOUND || queryError == ERROR_PATH_NOT_FOUND) &&
		           MoveFileExW(temporary.c_str(), target.value.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
	}
	if (!replaced)
	{
		const auto systemError = GetLastError();
		std::filesystem::remove(temporary, error);
		return Result<void>::Failure("Cannot atomically replace " + relativePath + ": Windows error " +
		                             ToUrsaString(std::to_string(systemError)));
	}
#else
	std::filesystem::rename(temporary, target.value, error);
	if (error)
	{
		std::filesystem::remove(temporary, error);
		return Result<void>::Failure("Cannot atomically replace " + relativePath + ": " + NativeError(error));
	}
#endif
	return {};
}

Result<void> DirectoryFileProvider::Append(const String& relativePath, const Vector<Ursa::UInt8>& bytes,
                                           bool createDirectories)
{
	auto target = ResolveForWrite(relativePath);
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
			return Result<void>::Failure("Cannot create directory for " + relativePath + ": " + NativeError(error));
		}
	}
	if (std::filesystem::is_directory(target.value, error) && !error)
	{
		return Result<void>::Failure("Resource path is a directory: " + relativePath);
	}
	std::ofstream output(target.value, std::ios::binary | std::ios::app);
	if (!output)
	{
		return Result<void>::Failure("Cannot open " + relativePath + " for append");
	}
	if (!bytes.empty())
	{
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	output.flush();
	return output ? Result<void>{} : Result<void>::Failure("Cannot append to " + relativePath);
}

Result<std::filesystem::path> DirectoryFileProvider::ResolveToDisk(const String& relativePath) const
{
	auto path = Resolve(relativePath);
	if (!path)
	{
		return path;
	}
	std::error_code error;
	if (!std::filesystem::is_regular_file(path.value, error) || error)
	{
		return Result<std::filesystem::path>::Failure("Resource not found in directory mount: " + relativePath);
	}
	return path;
}

Result<std::filesystem::path> DirectoryFileProvider::ResolveForWrite(const String& relativePath) const
{
	return Resolve(relativePath);
}

Result<void> DirectoryFileProvider::Remove(const String& relativePath)
{
	auto target = Resolve(relativePath);
	if (!target)
	{
		return Result<void>::Failure(target.error);
	}
	std::error_code error;
	if (std::filesystem::is_directory(target.value, error))
	{
		return Result<void>::Failure("Cannot remove a directory through VFS");
	}
	error.clear();
	std::filesystem::remove(target.value, error);
	return error ? Result<void>::Failure("Cannot remove " + relativePath + ": " + NativeError(error)) : Result<void>{};
}

Result<void> DirectoryFileProvider::CheckWritable(const String& relativePath) const
{
	auto target = Resolve(relativePath);
	if (!target)
	{
		return Result<void>::Failure(target.error);
	}
	std::error_code error;
	if (std::filesystem::is_directory(target.value, error))
	{
		return Result<void>::Failure("Resource path is a directory: " + relativePath);
	}
	return {};
}

Result<void> DirectoryFileProvider::Rename(const String& source, const String& destination)
{
	auto from = ResolveToDisk(source);
	auto to = Resolve(destination);
	if (!from || !to)
	{
		return Result<void>::Failure(!from ? from.error : to.error);
	}
	std::error_code error;
	std::filesystem::create_directories(to.value.parent_path(), error);
	if (!error)
	{
		std::filesystem::rename(from.value, to.value, error);
	}
	return error ? Result<void>::Failure("Cannot rename " + source + " to " + destination + ": " + NativeError(error))
	             : Result<void>{};
}
} // namespace Ursa
