#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Serialization/ResourceId.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <cstdint>
#include <filesystem>

namespace Ursa
{
enum class ResourceKind
{
	texture,
	material,
	scene,
	prefab
};

struct ResourceHeader
{
	ResourceKind kind;
	uint32_t version = 0;
	ResourceId id;
};

constexpr uint32_t resourceFormatVersion = 1;
constexpr uint64_t maxJsonResourceBytes = 16ull * 1024 * 1024;
constexpr uint64_t maxTextureResourceBytes = 256ull * 1024 * 1024;

Result<ResourceHeader> ReadResourceHeader(const std::filesystem::path& path);

Result<ResourceKind> ResourceKindForPath(const VirtualPath& path);

Result<ResourceHeader> ReadResourceHeader(const VirtualFileSystem& filesystem, const VirtualPath& path);
} // namespace Ursa
