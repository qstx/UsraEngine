#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <cstdint>
#include <string>

namespace Ursa
{
struct ResourceId
{
	Array<Ursa::UInt8, 16> bytes{};

	explicit operator bool() const;

	String ToString() const;

	static ResourceId Generate();

	static Result<ResourceId> Parse(const String& text);
};

bool operator==(const ResourceId& left, const ResourceId& right);

bool operator!=(const ResourceId& left, const ResourceId& right);
} // namespace Ursa
