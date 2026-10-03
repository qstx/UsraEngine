#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace Ursa
{
struct ResourceId
{
	Array<uint8_t, 16> bytes{};

	explicit operator bool() const;

	String ToString() const;

	static ResourceId Generate();

	static Result<ResourceId> Parse(const String& text);
};

bool operator==(const ResourceId& left, const ResourceId& right);

bool operator!=(const ResourceId& left, const ResourceId& right);
} // namespace Ursa
