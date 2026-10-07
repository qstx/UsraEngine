#pragma once

#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Core/Result.hpp>

namespace Ursa::PakFormat
{
inline constexpr Size headerBytes = 40;
inline constexpr UInt64 maxTableBytes = 64 * 1024 * 1024;
inline constexpr UInt32 maxEntries = 1000000;
inline constexpr UInt8 magic[8]{'U', 'R', 'S', 'A', 'P', 'A', 'K', 0};

inline void Append(Vector<UInt8>& bytes, UInt64 value, Size count)
{
	for (Size index = 0; index < count; ++index)
	{
		bytes.push_back(static_cast<UInt8>(value >> (index * 8)));
	}
}

inline void Put(Vector<UInt8>& bytes, Size offset, UInt64 value, Size count)
{
	for (Size index = 0; index < count; ++index)
	{
		bytes[offset + index] = static_cast<UInt8>(value >> (index * 8));
	}
}

inline Result<UInt64> Take(const Vector<UInt8>& bytes, Size& cursor, Size count)
{
	if (cursor > bytes.size() || count > bytes.size() - cursor || count > 8)
	{
		return Result<UInt64>::Failure("Truncated Pak directory field");
	}
	UInt64 value = 0;
	for (Size index = 0; index < count; ++index)
	{
		value |= static_cast<UInt64>(bytes[cursor++]) << (index * 8);
	}
	return Result<UInt64>::Success(value);
}
} // namespace Ursa::PakFormat
