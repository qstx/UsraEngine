#pragma once

#include <Core/Result.hpp>
#include <Serialization/Archive.hpp>
#include <Serialization/ResourceId.hpp>
#include <nlohmann/json.hpp>

namespace Ursa
{
struct AssetReference
{
	ResourceId guid;
	Ursa::UInt64 localFileId = 0;
};

Result<void> WriteAssetReference(ArchiveWriter& writer, const String& field, const AssetReference& reference);

Result<AssetReference> ReadAssetReference(const ArchiveReader& reader, const String& field);

nlohmann::json AssetReferenceToJson(const AssetReference& reference);

Result<AssetReference> AssetReferenceFromJson(const nlohmann::json& value, const String& location);
} // namespace Ursa
