#pragma once

#include <Serialization/ResourceId.hpp>
#include <Serialization/Serializable.hpp>
#include <nlohmann/json.hpp>

namespace Ursa
{
class ScriptableAsset final : public Serializable
{
public:
	String SerializationType() const override
	{
		return "ursa.ScriptableAsset";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	Result<void> SetDescription(ResourceId source, UInt64 fileId, String selectedTypeId, UInt32 selectedTypeVersion,
	                            nlohmann::json selectedPayload);

	Result<void> Validate() const;

	ResourceId SourceGuid() const
	{
		return sourceGuid;
	}

	UInt64 LocalFileId() const
	{
		return localFileId;
	}

	const String& TypeId() const
	{
		return typeId;
	}

	UInt32 TypeVersion() const
	{
		return typeVersion;
	}

	const nlohmann::json& Payload() const
	{
		return payload;
	}

private:
	ResourceId sourceGuid;
	UInt64 localFileId = 0;
	String typeId;
	UInt32 typeVersion = 1;
	nlohmann::json payload = nlohmann::json::object();
};
} // namespace Ursa
