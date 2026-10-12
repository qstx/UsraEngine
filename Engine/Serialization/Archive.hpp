#pragma once

#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <memory>

namespace Ursa
{
struct ArchiveStorage;
class SerializationContext;

class ArchiveWriter final
{
public:
	ArchiveWriter();

	void SetContext(SerializationContext* value)
	{
		context = value;
	}

	SerializationContext* Context() const
	{
		return context;
	}

	Result<void> Null(const String& field);

	Result<void> Write(const String& field, bool value);

	Result<void> Write(const String& field, Int64 value);

	Result<void> Write(const String& field, UInt64 value);

	Result<void> Write(const String& field, Float64 value);

	Result<void> Write(const String& field, const String& value);

	Result<void> WriteBytes(const String& field, const Vector<UInt8>& value);

	Result<ArchiveWriter> Object(const String& field);

	Result<ArchiveWriter> Array(const String& field);

	Result<ArchiveWriter> AddObject();

	Result<void> Append(Float64 value);

	Result<void> Append(const String& value);

private:
	friend struct ArchiveAccess;
	friend class JsonArchive;
	friend class BinaryArchive;
	ArchiveWriter(std::shared_ptr<ArchiveStorage> storage, String pointer, SerializationContext* context = nullptr);
	std::shared_ptr<ArchiveStorage> storage;
	String pointer;
	SerializationContext* context = nullptr;
};

class ArchiveReader final
{
public:
	ArchiveReader() = default;

	Result<bool> Boolean(const String& field) const;

	Result<Int64> Integer(const String& field) const;

	Result<UInt64> Unsigned(const String& field) const;

	Result<Float64> Number(const String& field) const;

	Result<String> Text(const String& field) const;

	Result<Vector<UInt8>> Bytes(const String& field) const;

	Result<ArchiveReader> Object(const String& field) const;

	Result<Vector<ArchiveReader>> Array(const String& field) const;

	Result<Float64> NumberValue() const;

	Result<String> TextValue() const;

	bool Contains(const String& field) const;

	Result<Vector<String>> Fields() const;

	bool IsNull(const String& field) const;

private:
	friend struct ArchiveAccess;
	friend class JsonArchive;
	friend class BinaryArchive;
	ArchiveReader(std::shared_ptr<ArchiveStorage> storage, String pointer);
	std::shared_ptr<ArchiveStorage> storage;
	String pointer;
};

class JsonArchive final
{
public:
	static Result<Vector<UInt8>> Encode(const ArchiveWriter& writer);

	static Result<ArchiveReader> Decode(const Vector<UInt8>& bytes, UInt64 maxEncodedBytes = 256ull * 1024 * 1024);
};

class BinaryArchive final
{
public:
	static Result<Vector<UInt8>> Encode(const ArchiveWriter& writer);

	static Result<ArchiveReader> Decode(const Vector<UInt8>& bytes, UInt64 maxEncodedBytes = 256ull * 1024 * 1024);
};
} // namespace Ursa
