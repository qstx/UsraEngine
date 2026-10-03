#include <core/StringInterop.hpp>
#include <serialization/ResourceId.hpp>
#include <random>

namespace Ursa
{
namespace
{
int HexDigit(char c)
{
	if (c >= '0' && c <= '9')
	{
		return c - '0';
	}
	if (c >= 'a' && c <= 'f')
	{
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F')
	{
		return c - 'A' + 10;
	}
	return -1;
}
} // namespace

ResourceId::operator bool() const
{
	for (uint8_t byte : bytes)
	{
		if (byte != 0)
		{
			return true;
		}
	}
	return false;
}

String ResourceId::ToString() const
{
	constexpr char digits[] = "0123456789abcdef";
	String result;
	result.reserve(36);
	for (size_t index = 0; index < bytes.size(); ++index)
	{
		if (index == 4 || index == 6 || index == 8 || index == 10)
		{
			result.push_back('-');
		}
		result.push_back(digits[bytes[index] >> 4]);
		result.push_back(digits[bytes[index] & 0x0f]);
	}
	return result;
}

ResourceId ResourceId::Generate()
{
	static thread_local std::mt19937_64 generator = []
	{
		std::random_device source;
		Array<uint32_t, 8> entropy{};
		for (uint32_t& word : entropy)
		{
			word = source();
		}
		std::seed_seq seed(entropy.begin(), entropy.end());
		return std::mt19937_64(seed);
	}();
	ResourceId result;
	for (size_t index = 0; index < result.bytes.size(); index += 8)
	{
		const uint64_t value = generator();
		for (size_t byte = 0; byte < 8; ++byte)
		{
			result.bytes[index + byte] = static_cast<uint8_t>(value >> (byte * 8));
		}
	}
	result.bytes[6] = static_cast<uint8_t>((result.bytes[6] & 0x0f) | 0x40);
	result.bytes[8] = static_cast<uint8_t>((result.bytes[8] & 0x3f) | 0x80);
	return result;
}

Result<ResourceId> ResourceId::Parse(const String& text)
{
	if (text.size() != 36)
	{
		return Result<ResourceId>::Failure("Resource ID must contain 36 characters");
	}
	ResourceId result;
	size_t cursor = 0;
	for (size_t index = 0; index < result.bytes.size(); ++index)
	{
		if (index == 4 || index == 6 || index == 8 || index == 10)
		{
			if (text[cursor++] != '-')
			{
				return Result<ResourceId>::Failure("Resource ID has invalid separator");
			}
		}
		const int high = HexDigit(text[cursor++]);
		const int low = HexDigit(text[cursor++]);
		if (high < 0 || low < 0)
		{
			return Result<ResourceId>::Failure("Resource ID contains non-hexadecimal digits");
		}
		result.bytes[index] = static_cast<uint8_t>((high << 4) | low);
	}
	if (!result)
	{
		return Result<ResourceId>::Failure("Resource ID cannot be zero");
	}
	return Result<ResourceId>::Success(result);
}

bool operator==(const ResourceId& left, const ResourceId& right)
{
	return left.bytes == right.bytes;
}

bool operator!=(const ResourceId& left, const ResourceId& right)
{
	return !(left == right);
}
} // namespace Ursa
