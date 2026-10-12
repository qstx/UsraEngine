#pragma once

#include <Material/Material.hpp>
#include <Scene/Component.hpp>

namespace Ursa
{
class RendererComponent : public Component
{
public:
	String SerializationType() const override = 0;

	const char* UniquenessGroup() const override;

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	std::shared_ptr<MaterialInstance> SharedMaterial() const;

	Vector<std::shared_ptr<MaterialInstance>> SharedMaterials() const;

	Result<void> SetSharedMaterial(std::shared_ptr<MaterialInstance> value);

	Result<void> SetSharedMaterials(Vector<std::shared_ptr<MaterialInstance>> values);

	Result<void> SetMaterial(std::shared_ptr<MaterialInstance> value);

	Result<void> SetMaterials(Vector<std::shared_ptr<MaterialInstance>> values);

	Result<std::shared_ptr<MaterialInstance>> Material();

	Result<Vector<std::shared_ptr<MaterialInstance>>> Materials();

protected:
	Result<void> RequireInstanceAccess() const;

private:
	Vector<std::shared_ptr<MaterialInstance>> materials;
	Vector<bool> instantiated;
};
} // namespace Ursa
