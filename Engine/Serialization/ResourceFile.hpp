#pragma once
#include <Core/Types.hpp>
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
	Texture,
	Material,
	Scene,
	Prefab,
	Shader,
	Mesh
};

struct ResourceHeader
{
	ResourceKind kind;
	Ursa::UInt32 version = 0;
	ResourceId id;
};

constexpr Ursa::UInt32 resourceFormatVersion = 1;
constexpr Ursa::UInt32 materialResourceFormatVersion = 2;
constexpr Ursa::UInt32 textureResourceFormatVersion = 2;
constexpr Ursa::UInt64 maxJsonResourceBytes = 16ull * 1024 * 1024;
constexpr Ursa::UInt64 maxTextureResourceBytes = 256ull * 1024 * 1024;
constexpr Ursa::UInt64 maxShaderResourceBytes = 256ull * 1024 * 1024;
constexpr Ursa::UInt64 maxMeshResourceBytes = 512ull * 1024 * 1024;

Result<ResourceKind> ResourceKindForPath(const VirtualPath& path);

Result<ResourceHeader> ReadResourceHeader(const VirtualFileSystem& filesystem, const VirtualPath& path);
} // namespace Ursa
