#include <Serialization/AssetReference.hpp>

#include <Core/StringInterop.hpp>

namespace Ursa
{
namespace
{
Result<AssetReference> ParseReference(const String& guidText, Ursa::UInt64 localFileId, const String& location)
{
	auto guid = ResourceId::Parse(guidText);
	if (!guid)
	{
		return Result<AssetReference>::Failure("Invalid asset GUID at " + location + ": " + guid.error);
	}
	return Result<AssetReference>::Success({guid.value, localFileId});
}
} // namespace

Result<void> WriteAssetReference(ArchiveWriter& writer, const String& field, const AssetReference& reference)
{
	if (!reference.guid)
	{
		return Result<void>::Failure("Cannot serialize an empty asset GUID in " + field);
	}
	auto object = writer.Object(field);
	if (!object)
	{
		return Result<void>::Failure(object.error);
	}
	auto result = object.value.Write("guid", reference.guid.ToString());
	return result ? object.value.Write("localFileId", reference.localFileId) : result;
}

Result<AssetReference> ReadAssetReference(const ArchiveReader& reader, const String& field)
{
	auto object = reader.Object(field);
	if (object)
	{
		auto guidText = object.value.Text("guid");
		auto localFileId = object.value.Unsigned("localFileId");
		if (!guidText || !localFileId)
		{
			return Result<AssetReference>::Failure("Invalid asset reference object in " + field);
		}
		return ParseReference(guidText.value, localFileId.value, field);
	}
	// Read legacy string references as file-level assets during project migration.
	auto legacyText = reader.Text(field);
	return legacyText ? ParseReference(legacyText.value, 0, field)
	                  : Result<AssetReference>::Failure("Invalid asset reference in " + field);
}

nlohmann::json AssetReferenceToJson(const AssetReference& reference)
{
	return {{"guid", ToStdString(reference.guid.ToString())}, {"localFileId", reference.localFileId}};
}

Result<AssetReference> AssetReferenceFromJson(const nlohmann::json& value, const String& location)
{
	if (value.is_string())
	{
		return ParseReference(ToUrsaString(value.get<std::string>()), 0, location);
	}
	if (!value.is_object() || !value.contains("guid") || !value["guid"].is_string() || !value.contains("localFileId") ||
	    !value["localFileId"].is_number_unsigned())
	{
		return Result<AssetReference>::Failure("Invalid asset reference at " + location);
	}
	return ParseReference(ToUrsaString(value["guid"].get<std::string>()), value["localFileId"].get<Ursa::UInt64>(),
	                      location);
}
} // namespace Ursa
