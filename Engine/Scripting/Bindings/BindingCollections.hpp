#pragma once

#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Core/Containers.hpp>

namespace Ursa::Scripting::Bindings
{
template <class W, class T, class Convert>
Vector<T> ReadArray(const DataView& value, Convert convert, UInt64 capacity = maximumBytes)
{
	Validate(value.data, value.length, sizeof(W), value.present, value.reserved);
	if (!value.present || value.length > capacity / sizeof(T))
	{
		throw std::runtime_error("Required typed array is null or exceeds capacity");
	}
	const auto* source = static_cast<const W*>(value.data);
	Vector<T> result;
	result.reserve(Size(value.length));
	for (UInt64 index = 0; index < value.length; ++index)
	{
		result.push_back(convert(source[index]));
	}
	return result;
}

template <class W, class T, class Convert>
DataView WriteArray(BindingArena& arena, const Vector<T>& values, Convert convert)
{
	auto* result = static_cast<W*>(arena.Allocate(values.size(), sizeof(W)));
	for (Size index = 0; index < values.size(); ++index)
	{
		result[index] = convert(values[index]);
	}
	return {result, values.size(), 1, 0};
}
} // namespace Ursa::Scripting::Bindings
