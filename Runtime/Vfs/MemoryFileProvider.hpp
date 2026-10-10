#pragma once

#include <Vfs/VirtualFileProvider.hpp>
#include <mutex>
#include <string>

namespace Ursa
{
// Private staging store for validation before an atomic persistent commit.
class MemoryFileProvider final : public VirtualFileProvider
{
private:
	struct LeaseState
	{
		std::mutex mutex;
		Set<String> paths;
	};

	class Lease final : public VirtualFileLease
	{
	public:
		Lease(std::shared_ptr<LeaseState> state, String path) : state(std::move(state)), path(std::move(path))
		{
		}

		~Lease() override
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			state->paths.erase(path);
		}

	private:
		std::shared_ptr<LeaseState> state;
		String path;
	};

public:
	Result<std::shared_ptr<VirtualFileLease>> AcquireExclusiveLease(const String& path) override
	{
		std::lock_guard<std::mutex> lock(leaseState->mutex);
		if (!leaseState->paths.insert(path).second)
		{
			return Result<std::shared_ptr<VirtualFileLease>>::Failure("Project import is busy: " + path);
		}
		return Result<std::shared_ptr<VirtualFileLease>>::Success(std::make_shared<Lease>(leaseState, path));
	}

	String Identity() const override
	{
		return "memory:" + String(std::to_string(reinterpret_cast<UIntPtr>(this)).c_str());
	}

	String SourceLabel() const override
	{
		return "memory";
	}

	Result<bool> Exists(const String& path) const override
	{
		std::lock_guard<std::mutex> lock(mutex);
		return Result<bool>::Success(files.find(path) != files.end());
	}

	Result<UInt64> Size(const String& path) const override
	{
		std::lock_guard<std::mutex> lock(mutex);
		auto found = files.find(path);
		return found == files.end() ? Result<UInt64>::Failure("Missing memory file " + path)
		                            : Result<UInt64>::Success(found->second.size());
	}

	Result<Vector<UInt8>> ReadRange(const String& path, UInt64 offset, UInt64 size) const override
	{
		std::lock_guard<std::mutex> lock(mutex);
		auto found = files.find(path);
		if (found == files.end() || offset > found->second.size() || size > found->second.size() - offset)
		{
			return Result<Vector<UInt8>>::Failure("Invalid memory file range " + path);
		}
		return Result<Vector<UInt8>>::Success(
		    Vector<UInt8>(found->second.begin() + SizeType(offset), found->second.begin() + SizeType(offset + size)));
	}

	Result<Vector<String>> List() const override
	{
		std::lock_guard<std::mutex> lock(mutex);
		Vector<String> entries;
		for (const auto& file : files)
		{
			entries.push_back(file.first);
		}
		return Result<Vector<String>>::Success(std::move(entries));
	}

	Result<void> WriteAtomic(const String& path, const Vector<UInt8>& bytes, bool) override
	{
		std::lock_guard<std::mutex> lock(mutex);
		files[path] = bytes;
		return {};
	}

	Result<void> Append(const String& path, const Vector<UInt8>& bytes, bool) override
	{
		std::lock_guard<std::mutex> lock(mutex);
		auto& value = files[path];
		value.insert(value.end(), bytes.begin(), bytes.end());
		return {};
	}

	Result<void> Remove(const String& path) override
	{
		std::lock_guard<std::mutex> lock(mutex);
		files.erase(path);
		return {};
	}

private:
	using SizeType = Ursa::Size;
	mutable std::mutex mutex;
	Map<String, Vector<UInt8>> files;
	std::shared_ptr<LeaseState> leaseState = std::make_shared<LeaseState>();
};
} // namespace Ursa
