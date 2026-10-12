#pragma once

#include <Mesh/Mesh.hpp>
#include <Scene/RendererComponent.hpp>

namespace Ursa
{
class SkinnedMeshRendererComponent final : public RendererComponent
{
public:
	String SerializationType() const override;

	UInt32 SerializationVersion() const override;

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	MeshPtr SharedMesh() const;

	Result<void> SetSharedMesh(MeshPtr value);

private:
	MeshPtr mesh;
};
} // namespace Ursa
