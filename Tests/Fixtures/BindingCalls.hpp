#pragma once

#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <stdexcept>

namespace Ursa::Test::BindingCalls
{
namespace B = Ursa::Scripting::Bindings;

template <class T, class F, class... Args> B::CallbackResult<T> Invoke(F function, void* context, Args... arguments)
{
	B::CallbackResult<T> result;
	result.status = function(context, arguments..., &result.value, &result.outcome);
	return result;
}

template <class F, class... Args> B::CallbackResult<void> InvokeVoid(F function, void* context, Args... arguments)
{
	B::CallbackResult<void> result;
	result.status = function(context, arguments..., &result.outcome);
	return result;
}

template <class T> T Value(B::CallbackResult<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.Error()));
	}
	return result.value;
}

inline void Complete(B::CallbackResult<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.Error()));
	}
}

inline String Text(B::CallbackResult<B::Utf8View> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.Error()));
	}
	return B::ReadString(result.value);
}

inline Vector<UInt8> Bytes(B::CallbackResult<B::DataView> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.Error()));
	}
	return B::ReadArray<UInt8, UInt8>(result.value, [](UInt8 value) { return value; });
}

inline B::Utf8View StringView(const String& value)
{
	return {reinterpret_cast<const UInt8*>(value.data()), value.size(), 1, 0};
}

template <Size N> B::Utf8View StringView(const char (&value)[N])
{
	return {reinterpret_cast<const UInt8*>(value), N - 1, 1, 0};
}
} // namespace Ursa::Test::BindingCalls
