#pragma once

#include <Serialization/Archive.hpp>
#include <nlohmann/json.hpp>

namespace Ursa
{
// Resource envelopes and legacy test codecs use this bridge. Persistent model
// implementations use ArchiveReader/Writer fields instead of JSON types.
Result<ArchiveReader> ArchiveFromJson(const nlohmann::json& document);

Result<nlohmann::json> ArchiveToJson(const ArchiveWriter& writer);

Result<nlohmann::json> ArchiveToJson(const ArchiveReader& reader);

Result<ArchiveReader> ArchiveSnapshot(const ArchiveWriter& writer);

Result<void> ArchiveWriteJson(ArchiveWriter& writer, const nlohmann::json& document);
} // namespace Ursa
