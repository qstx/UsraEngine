#pragma once

#include <Vfs/VirtualFileSystem.hpp>

namespace Ursa
{
class UserDataStore final
{
public:
	explicit UserDataStore(VirtualFileSystem& filesystem) : filesystem(filesystem)
	{
	}

	Result<void> SaveSettings(const String& name, const Vector<UInt8>& bytes)
	{
		return Save("Settings/", name, bytes);
	}

	Result<Vector<UInt8>> LoadSettings(const String& name) const
	{
		return Load("Settings/", name);
	}

	Result<void> SaveGame(const String& name, const Vector<UInt8>& bytes)
	{
		return Save("Saves/", name, bytes);
	}

	Result<Vector<UInt8>> LoadGame(const String& name) const
	{
		return Load("Saves/", name);
	}

private:
	Result<VirtualPath> Path(const char* folder, const String& name) const
	{
		if (name.empty() || name.find_first_of("/\\:") != String::npos || name == "." || name == "..")
		{
			return Result<VirtualPath>::Failure("Invalid user data name");
		}
		return VirtualPath::Parse("user:" + String(folder) + name);
	}

	Result<void> Save(const char* folder, const String& name, const Vector<UInt8>& bytes)
	{
		auto path = Path(folder, name);
		return path ? filesystem.WriteAtomic(path.value, bytes) : Result<void>::Failure(path.error);
	}

	Result<Vector<UInt8>> Load(const char* folder, const String& name) const
	{
		auto path = Path(folder, name);
		return path ? filesystem.Read(path.value) : Result<Vector<UInt8>>::Failure(path.error);
	}

	VirtualFileSystem& filesystem;
};
} // namespace Ursa
