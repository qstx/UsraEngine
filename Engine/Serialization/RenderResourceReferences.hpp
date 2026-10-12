#pragma once

#include <Material/Material.hpp>
#include <Mesh/Mesh.hpp>
#include <Serialization/SerializationContext.hpp>

namespace Ursa
{
class RenderResourceReferences
{
public:
	virtual ~RenderResourceReferences() = default;

	virtual Result<void> WriteLocalMesh(ArchiveWriter& writer, const String& field, const MeshPtr& mesh) = 0;

	virtual Result<void> WriteLocalMaterial(ArchiveWriter& writer, const String& field,
	                                        const std::shared_ptr<MaterialInstance>& material) = 0;

	virtual Result<MeshPtr> ReadLocalMesh(const ArchiveReader& reader, const String& field) = 0;

	virtual Result<std::shared_ptr<MaterialInstance>> ReadLocalMaterial(const ArchiveReader& reader,
	                                                                    const String& field) = 0;
};

Result<void> WriteMeshBinding(ArchiveWriter& writer, const String& field, const MeshPtr& mesh);

Result<MeshPtr> ReadMeshBinding(const ArchiveReader& reader, const String& field, SerializationContext& context);

Result<void> WriteMaterialBinding(ArchiveWriter& writer, const String& field,
                                  const std::shared_ptr<MaterialInstance>& material);

Result<std::shared_ptr<MaterialInstance>> ReadMaterialBinding(const ArchiveReader& reader, const String& field,
                                                              SerializationContext& context);
} // namespace Ursa
