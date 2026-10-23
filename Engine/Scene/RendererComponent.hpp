#pragma once

#include <Material/Material.hpp>
#include <Scene/Component.hpp>

namespace Ursa
{
namespace Rendering
{
class RenderScene;
}

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

	bool CastShadows() const
	{
		return castShadows;
	}

	void SetCastShadows(bool value)
	{
		castShadows = value;
	}

	bool ReceiveShadows() const
	{
		return receiveShadows;
	}

	void SetReceiveShadows(bool value)
	{
		receiveShadows = value;
	}

	Result<std::shared_ptr<MaterialInstance>> Material();

	Result<Vector<std::shared_ptr<MaterialInstance>>> Materials();

protected:
	Result<void> RequireInstanceAccess() const;

private:
	friend class Rendering::RenderScene;
	Vector<std::shared_ptr<MaterialInstance>> materials;
	Vector<bool> instantiated;
	bool castShadows = true;
	bool receiveShadows = true;
};
} // namespace Ursa
