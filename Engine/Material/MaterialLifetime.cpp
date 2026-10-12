#include <Material/Material.hpp>

namespace Ursa
{
bool MaterialInstance::IsValid() const
{
	return alive;
}

Result<void> MaterialInstance::Destroy()
{
	if (assetIdentity.guid)
	{
		return Result<void>::Failure("Cannot destroy a persistent Material asset; clone it first");
	}
	if (!alive)
	{
		return Result<void>::Failure("Material has already been destroyed");
	}
	alive = false;
	return {};
}

AssetReference MaterialInstance::AssetIdentity() const
{
	return assetIdentity;
}

Result<void> MaterialInstance::BindAssetIdentity(AssetReference reference)
{
	if (!alive || !reference.guid ||
	    (assetIdentity.guid &&
	     (assetIdentity.guid != reference.guid || assetIdentity.localFileId != reference.localFileId)))
	{
		return Result<void>::Failure("Material asset identity is invalid or already bound");
	}
	assetIdentity = reference;
	return {};
}

AssetReference MaterialInstance::TextureReference(MaterialTextureSlot slot) const
{
	const Size index = Size(slot);
	return index < textures.size() ? AssetReference{textureResourceIds[index], textureLocalFileIds[index]}
	                               : AssetReference{};
}

Result<void> MaterialInstance::SetTextureReference(MaterialTextureSlot slot, AssetReference reference,
                                                   MaterialTexturePtr texture)
{
	const Size index = Size(slot);
	if (!alive || index >= textures.size() || (texture && !reference.guid) ||
	    (!reference.guid && reference.localFileId))
	{
		return Result<void>::Failure("Invalid material texture resource binding");
	}
	MaterialInstance candidate(*this);
	auto result = candidate.SetTexture(slot, std::move(texture));
	if (result)
	{
		result = candidate.SetTextureResourceId(slot, reference.guid);
	}
	if (!result)
	{
		return result;
	}
	candidate.textureLocalFileIds[index] = reference.localFileId;
	for (auto& property : candidate.propertyOverrides)
	{
		if (property.second.type == Shader::PropertyType::Texture2D &&
		    property.second.texture == candidate.textures[index] && property.second.textureId == reference.guid)
		{
			property.second.textureLocalFileId = reference.localFileId;
		}
	}
	*this = std::move(candidate);
	return {};
}

} // namespace Ursa
