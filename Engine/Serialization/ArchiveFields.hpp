#pragma once

#include <Serialization/Archive.hpp>
#include <limits>

namespace Ursa
{
inline Result<void> WriteNumbers(ArchiveWriter& writer, const String& field, std::initializer_list<Float64> values)
{
	auto array = writer.Array(field);
	if (!array)
	{
		return Result<void>::Failure(array.error);
	}
	for (Float64 value : values)
	{
		auto result = array.value.Append(value);
		if (!result)
		{
			return result;
		}
	}
	return {};
}

inline Result<Vector<Float32>> ReadNumbers(const ArchiveReader& reader, const String& field, Size count)
{
	auto array = reader.Array(field);
	if (!array || array.value.size() != count)
	{
		return Result<Vector<Float32>>::Failure("Invalid archive array " + field);
	}
	Vector<Float32> values;
	for (const auto& item : array.value)
	{
		auto number = item.NumberValue();
		if (!number || number.value < -(std::numeric_limits<Float32>::max)() ||
		    number.value > (std::numeric_limits<Float32>::max)())
		{
			return Result<Vector<Float32>>::Failure("Invalid archive number in " + field);
		}
		values.push_back(static_cast<Float32>(number.value));
	}
	return Result<Vector<Float32>>::Success(std::move(values));
}
} // namespace Ursa
