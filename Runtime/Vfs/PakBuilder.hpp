#pragma once

#include <Vfs/PakArchive.hpp>

namespace Ursa
{
struct PakEntryData
{
	VirtualPath path;
	Vector<UInt8> bytes;
};

class PakBuilder final
{
public:
	static Result<Vector<UInt8>> Encode(Vector<PakEntryData> entries);

	static Result<void> Build(const VirtualFileSystem& source, const Vector<String>& schemes,
	                          VirtualFileSystem& destination, const VirtualPath& packagePath);
};
} // namespace Ursa
