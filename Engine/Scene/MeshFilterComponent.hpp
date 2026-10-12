#pragma once

#include <Mesh/Mesh.hpp>
#include <Scene/Component.hpp>

namespace Ursa
{
class MeshFilterComponent final : public Component
{
public:
	String SerializationType() const override;

	const char* UniquenessGroup() const override;

	bool SupportsEnabled() const override;

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	MeshPtr SharedMesh() const;

	Result<void> SetSharedMesh(MeshPtr value);

	Result<void> SetMesh(MeshPtr value);

	Result<MeshPtr> Mesh();

private:
	MeshPtr mesh;
	bool instantiated = false;
};
} // namespace Ursa
