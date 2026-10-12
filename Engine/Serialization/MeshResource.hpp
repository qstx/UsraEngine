#pragma once

#include <Mesh/Mesh.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Serialization/SerializationContext.hpp>

namespace Ursa
{
struct LoadedMeshResource
{
	ResourceId id;
	UInt64 localFileId = 0;
	MeshPtr mesh;
};

Result<Vector<UInt8>> EncodeMeshResource(ResourceId id, const Mesh& mesh, UInt64 localFileId = 0, bool binary = true);

// Binary is the default storage. JSON uses the same versioned Archive fields.
Result<void> SaveMeshResource(VirtualFileSystem& files, const VirtualPath& path, ResourceId id, const Mesh& mesh,
                              bool binary = true);

Result<LoadedMeshResource> LoadMeshResource(const VirtualFileSystem& files, const VirtualPath& path,
                                            SerializationContext& context);
} // namespace Ursa
