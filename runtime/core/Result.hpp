#pragma once
#include <core/Containers.hpp>
#include <utility>

namespace Ursa
{
template <class T> struct Result
{
	T value{};
	String error;

	explicit operator bool() const
	{
		return error.empty();
	}

	static Result Success(T v)
	{
		Result r;
		r.value = std::move(v);
		return r;
	}

	static Result Failure(String e)
	{
		Result r;
		r.error = std::move(e);
		return r;
	}
};

template <> struct Result<void>
{
	String error;

	explicit operator bool() const
	{
		return error.empty();
	}

	static Result Success()
	{
		return {};
	}

	static Result Failure(String e)
	{
		return {std::move(e)};
	}
};
} // namespace Ursa
