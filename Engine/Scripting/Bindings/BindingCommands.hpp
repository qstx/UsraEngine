#pragma once

#include <Scripting/Bindings/BindingRuntime.hpp>
#include <limits>
#include <type_traits>

namespace Ursa::Scripting::Bindings
{
inline constexpr UInt32 bindingCommandVersion = 1;
inline constexpr UInt32 bindingCommandHeaderSize = 48;
inline constexpr UInt32 maximumCommandBytes = 64 * 1024 * 1024;

inline void ValidateCommandPlatform()
{
	const UInt32 one = 1;
	if (*reinterpret_cast<const UInt8*>(&one) != 1)
	{
		throw std::runtime_error("Binding command storage requires little endian");
	}
}

// Primitive storage only. Generated codecs define command identities and every payload field.
class BindingCommandWriter
{
public:
	explicit BindingCommandWriter(const Array<UInt8, 32>& identity) : fingerprint(identity)
	{
		ValidateCommandPlatform();
		Clear();
	}

	void RequireIdentity(const Array<UInt8, 32>& expected) const
	{
		if (fingerprint != expected)
		{
			throw std::runtime_error("The command writer belongs to a different binding contract");
		}
	}

	void Clear()
	{
		bytes.clear();
		recordStart = noRecord;
		count = 0;
		Scalar(bindingCommandVersion);
		Scalar(bindingCommandHeaderSize);
		Scalar(count);
		Scalar(bindingCommandHeaderSize);
		bytes.insert(bytes.end(), fingerprint.begin(), fingerprint.end());
	}

	void Begin(UInt32 opcode)
	{
		if (recordStart != noRecord || bytes.size() > maximumCommandBytes - 8)
		{
			throw std::runtime_error("Command records cannot be nested or exceed the batch budget");
		}
		recordStart = bytes.size();
		try
		{
			Scalar(opcode);
			Scalar(UInt32{0});
		}
		catch (...)
		{
			Cancel();
			throw;
		}
	}

	void End()
	{
		if (recordStart == noRecord)
		{
			throw std::runtime_error("No command record is open");
		}
		while (bytes.size() % 8)
		{
			Scalar(UInt8{0});
		}
		const UInt32 size = UInt32(bytes.size() - recordStart);
		std::memcpy(bytes.data() + recordStart + 4, &size, 4);
		++count;
		recordStart = noRecord;
	}

	void Cancel()
	{
		if (recordStart != noRecord)
		{
			bytes.resize(recordStart);
			recordStart = noRecord;
		}
	}

	const Vector<UInt8>& Bytes()
	{
		if (recordStart != noRecord)
		{
			throw std::runtime_error("A command record is still open");
		}
		const UInt32 size = UInt32(bytes.size());
		std::memcpy(bytes.data() + 8, &count, 4);
		std::memcpy(bytes.data() + 12, &size, 4);
		return bytes;
	}

	template <class T> void Scalar(T value)
	{
		static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>);
		if (bytes.size() > maximumCommandBytes - sizeof(T))
		{
			throw std::runtime_error("The command batch exceeds its transfer budget");
		}
		const Size offset = bytes.size();
		bytes.resize(offset + sizeof(T));
		std::memcpy(bytes.data() + offset, &value, sizeof(T));
	}

	void Collection(UInt64 length, UInt32 present, UInt32 minimumElementBytes)
	{
		if (!minimumElementBytes || present > 1 || (!present && length) || bytes.size() > maximumCommandBytes - 12 ||
		    length > (maximumCommandBytes - bytes.size() - 12) / minimumElementBytes)
		{
			throw std::runtime_error("The command collection exceeds its transfer budget");
		}
		Scalar(present);
		Scalar(length);
	}

	void StringValue(const Utf8View& value, bool nullable)
	{
		ValidateUtf8(value, nullable);
		Collection(value.length, value.present, 1);
		if (value.length)
		{
			bytes.insert(bytes.end(), value.data, value.data + Size(value.length));
		}
	}

private:
	static constexpr Size noRecord = std::numeric_limits<Size>::max();
	Array<UInt8, 32> fingerprint;
	Vector<UInt8> bytes;
	Size recordStart = noRecord;
	UInt32 count = 0;
};

class BindingCommandReader
{
public:
	BindingCommandReader(const void* data, Size byteCount, const Array<UInt8, 32>& identity)
	    : bytes(static_cast<const UInt8*>(data)), size(byteCount)
	{
		ValidateCommandPlatform();
		if (!data || size < bindingCommandHeaderSize || size > maximumCommandBytes)
		{
			throw std::runtime_error("The command batch byte range is invalid");
		}
		recordEnd = size;
		if (Scalar<UInt32>() != bindingCommandVersion || Scalar<UInt32>() != bindingCommandHeaderSize)
		{
			throw std::runtime_error("The command batch version is invalid");
		}
		count = Scalar<UInt32>();
		if (Scalar<UInt32>() != size || count > (size - bindingCommandHeaderSize) / 8 ||
		    std::memcmp(Take(32), identity.data(), identity.size()) != 0)
		{
			throw std::runtime_error("The command batch size, count or contract identity is invalid");
		}
		recordEnd = offset;
	}

	bool Next(UInt32& opcode)
	{
		if (offset != recordEnd)
		{
			throw std::runtime_error("The previous command record was not consumed");
		}
		if (records == count)
		{
			if (offset != size)
			{
				throw std::runtime_error("The command batch contains trailing bytes");
			}
			return false;
		}
		recordEnd = size;
		opcode = Scalar<UInt32>();
		const UInt32 length = Scalar<UInt32>();
		if (length < 8 || length % 8 || length - 8 > size - offset)
		{
			throw std::runtime_error("The command record range is invalid");
		}
		recordEnd = offset + length - 8;
		++records;
		return true;
	}

	UInt32 RecordCount() const
	{
		return count;
	}

	void End()
	{
		if (recordEnd - offset > 7)
		{
			throw std::runtime_error("The command payload has trailing data");
		}
		while (offset < recordEnd)
		{
			if (Scalar<UInt8>() != 0)
			{
				throw std::runtime_error("The command padding must be zero");
			}
		}
	}

	template <class T> T Scalar()
	{
		static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>);
		T value;
		std::memcpy(&value, Take(sizeof(T)), sizeof(T));
		return value;
	}

	UInt32 Presence()
	{
		const UInt32 value = Scalar<UInt32>();
		if (value > 1)
		{
			throw std::runtime_error("Invalid command presence marker");
		}
		return value;
	}

	Utf8View StringValue(bool nullable)
	{
		const auto collection = Collection(nullable, 1);
		const auto* value = collection.first ? Take(Size(collection.first)) : nullptr;
		Utf8View result{value, collection.first, collection.second, 0};
		ValidateUtf8(result, nullable);
		return result;
	}

	template <class T, class F>
	DataView ArrayValue(BindingArena& arena, bool nullable, UInt32 minimumElementBytes, F&& read)
	{
		const auto collection = Collection(nullable, minimumElementBytes);
		auto* result = static_cast<T*>(arena.Allocate(collection.first, sizeof(T)));
		for (UInt64 index = 0; index < collection.first; ++index)
		{
			result[index] = read();
		}
		return {result, collection.first, collection.second, 0};
	}

private:
	const UInt8* Take(Size length)
	{
		if (length > recordEnd - offset)
		{
			throw std::runtime_error("The command payload is truncated");
		}
		const auto* result = bytes + offset;
		offset += length;
		return result;
	}

	std::pair<UInt64, UInt32> Collection(bool nullable, UInt32 minimumElementBytes)
	{
		const UInt32 present = Presence();
		const UInt64 length = Scalar<UInt64>();
		if ((!present && (!nullable || length)) || !minimumElementBytes ||
		    length > (recordEnd - offset) / minimumElementBytes)
		{
			throw std::runtime_error("The command collection range or presence is invalid");
		}
		return {length, present};
	}

	const UInt8* bytes;
	Size size;
	Size offset = 0;
	Size recordEnd = 0;
	UInt32 count = 0;
	UInt32 records = 0;
};
} // namespace Ursa::Scripting::Bindings
