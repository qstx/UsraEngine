#include <Core/StringInterop.hpp>
#include <ScriptableAssets/ScriptableAsset.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
#include <Serialization/AssetReference.hpp>
#include <limits>

namespace Ursa
{
namespace
{
Result<void> ValidateReferences(const nlohmann::json& value, Size depth = 0)
{
	if (depth > 64)
	{
		return Result<void>::Failure("ScriptableAsset fields exceed the supported nesting depth");
	}
	if (value.is_object() && value.contains("$ursaReference"))
	{
		const auto& reference = value["$ursaReference"];
		if (!reference.is_object())
		{
			return Result<void>::Failure("ScriptableAsset reference is malformed");
		}
		const auto kind = reference.value("Kind", std::string{});
		if ((kind == "GameObject" || kind == "Component") && !reference.value("Id", std::string{}).empty())
		{
			return Result<void>::Failure("Persistent ScriptableAsset cannot reference a scene instance");
		}
		if (reference.contains("Resource") && reference["Resource"].is_object() &&
		    reference["Resource"].value("kind", std::string{}) == "local")
		{
			return Result<void>::Failure("Persistent ScriptableAsset cannot reference a document-local resource");
		}
		if (kind == "Shader" || kind == "ComputeShader" || kind == "Texture" || kind == "ScriptableObject")
		{
			if (!reference.contains("Resource"))
			{
				return Result<void>::Failure("ScriptableAsset resource reference has no persistent identity");
			}
			auto resource = reference["Resource"];
			if (resource.is_object() && resource.contains("localFileId") &&
			    resource["localFileId"].is_number_integer() && !resource["localFileId"].is_number_unsigned() &&
			    resource["localFileId"].get<Int64>() >= 0)
			{
				resource["localFileId"] = resource["localFileId"].get<UInt64>();
			}
			auto identity = AssetReferenceFromJson(resource, "ScriptableAsset field");
			if (!identity || !identity.value.guid || (kind == "ScriptableObject" && !identity.value.localFileId))
			{
				return Result<void>::Failure(identity ? "ScriptableAsset resource reference identity is empty"
				                                      : identity.error);
			}
		}
	}
	if (value.is_structured())
	{
		for (const auto& child : value)
		{
			auto valid = ValidateReferences(child, depth + 1);
			if (!valid)
			{
				return valid;
			}
		}
	}
	return {};
}
} // namespace

Result<void> ScriptableAsset::Validate() const
{
	if (!sourceGuid || localFileId == 0 || typeId.empty() || typeVersion == 0 || !payload.is_object() ||
	    !payload.contains("Fields") || !payload["Fields"].is_object())
	{
		return Result<void>::Failure("ScriptableAsset requires a stable type identity and object Fields payload");
	}
	try
	{
		if (payload.contains("FormatVersion") && payload["FormatVersion"] != 1)
		{
			return Result<void>::Failure("Unsupported ScriptableAsset field payload format version");
		}
		if (payload.contains("ScriptType") && payload["ScriptType"] != ToStdString(typeId))
		{
			return Result<void>::Failure("ScriptableAsset payload type differs from its descriptor");
		}
		if (payload.contains("Version") && payload["Version"] != typeVersion)
		{
			return Result<void>::Failure("ScriptableAsset payload version differs from its descriptor");
		}
		return ValidateReferences(payload["Fields"]);
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Invalid ScriptableAsset payload: " + ToUrsaString(error.what()));
	}
}

Result<void> ScriptableAsset::SetDescription(ResourceId source, UInt64 fileId, String selectedTypeId,
                                             UInt32 selectedTypeVersion, nlohmann::json selectedPayload)
{
	ScriptableAsset candidate;
	candidate.sourceGuid = source;
	candidate.localFileId = fileId;
	candidate.typeId = std::move(selectedTypeId);
	candidate.typeVersion = selectedTypeVersion;
	candidate.payload = std::move(selectedPayload);
	auto valid = candidate.Validate();
	if (!valid)
	{
		return valid;
	}
	*this = std::move(candidate);
	return {};
}

Result<void> ScriptableAsset::Serialize(ArchiveWriter& writer) const
{
	auto valid = Validate();
	if (!valid)
	{
		return valid;
	}
	for (auto result : {writer.Write("sourceGuid", sourceGuid.ToString()), writer.Write("localFileId", localFileId),
	                    writer.Write("typeId", typeId), writer.Write("typeVersion", UInt64(typeVersion))})
	{
		if (!result)
		{
			return result;
		}
	}
	auto object = writer.Object("payload");
	return object ? ArchiveWriteJson(object.value, payload) : Result<void>::Failure(object.error);
}

Result<void> ScriptableAsset::Deserialize(const ArchiveReader& reader, SerializationContext&)
{
	auto source = reader.Text("sourceGuid");
	auto fileId = reader.Unsigned("localFileId");
	auto type = reader.Text("typeId");
	auto version = reader.Unsigned("typeVersion");
	auto fields = reader.Object("payload");
	if (!source || !fileId || !type || !version || !fields || version.value > (std::numeric_limits<UInt32>::max)())
	{
		return Result<void>::Failure("ScriptableAsset archive descriptor is incomplete or invalid");
	}
	auto guid = ResourceId::Parse(source.value);
	auto data = ArchiveToJson(fields.value);
	if (!guid || !data)
	{
		return Result<void>::Failure(!guid ? guid.error : data.error);
	}
	return SetDescription(guid.value, fileId.value, std::move(type.value), UInt32(version.value),
	                      std::move(data.value));
}
} // namespace Ursa
