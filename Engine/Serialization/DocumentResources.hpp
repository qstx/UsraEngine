#pragma once

#include <Serialization/RenderResourceReferences.hpp>
#include <Serialization/JsonArchiveInterop.hpp>

namespace Ursa
{
// Owns one document's transient resource graph. Persistent assets remain external.
class DocumentResources final : public RenderResourceReferences
{
public:
	Result<void> WriteLocalMesh(ArchiveWriter& writer, const String& field, const MeshPtr& mesh) override;

	Result<void> WriteLocalMaterial(ArchiveWriter& writer, const String& field,
	                                const std::shared_ptr<MaterialInstance>& material) override;

	Result<MeshPtr> ReadLocalMesh(const ArchiveReader& reader, const String& field) override;

	Result<std::shared_ptr<MaterialInstance>> ReadLocalMaterial(const ArchiveReader& reader,
	                                                            const String& field) override;

	Result<nlohmann::json> Encode(SerializationContext& context) const;

	Result<void> Decode(const nlohmann::json& document, SerializationContext& context,
	                    bool regenerateIdentities = false);

	Result<void> Merge(const DocumentResources& other);

private:
	Result<void> WriteReference(ArchiveWriter& writer, const String& field, ResourceId id);

	Map<String, MeshPtr> meshes;
	Map<String, std::shared_ptr<MaterialInstance>> materials;
};

// Restore a previous provider when loading nested Prefab documents.
class DocumentResourceScope final
{
public:
	DocumentResourceScope(SerializationContext& context, DocumentResources& resources);

	~DocumentResourceScope();

private:
	SerializationContext& context;
	RenderResourceReferences* previous = nullptr;
};
} // namespace Ursa
