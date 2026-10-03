#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <serialization/ResourceFile.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace ursa {
Result<nlohmann::json> readJsonResource(const std::filesystem::path &path, ResourceKind expectedKind);

Result<void> writeJsonResource(const std::filesystem::path &path, const nlohmann::json &document);
} // namespace ursa
