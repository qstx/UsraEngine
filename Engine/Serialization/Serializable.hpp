#pragma once

#include <Serialization/Archive.hpp>

namespace Ursa
{
class SerializationContext;

class Serializable
{
public:
	virtual ~Serializable() = default;

	virtual String SerializationType() const
	{
		return {};
	}

	virtual UInt32 SerializationVersion() const
	{
		return 1;
	}

	virtual Result<void> Serialize(ArchiveWriter&) const
	{
		return Result<void>::Failure("Object has no persistent serialization implementation");
	}

	virtual Result<void> Deserialize(const ArchiveReader&, SerializationContext&)
	{
		return Result<void>::Failure("Object has no persistent deserialization implementation");
	}
};
} // namespace Ursa
