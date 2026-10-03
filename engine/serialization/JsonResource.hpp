#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <serialization/ResourceFile.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Ursa
{
Result<nlohmann::json> ReadJsonResource(const std::filesystem::path& path, ResourceKind expectedKind);

Result<void> WriteJsonResource(const std::filesystem::path& path, const nlohmann::json& document);
} // namespace Ursa
