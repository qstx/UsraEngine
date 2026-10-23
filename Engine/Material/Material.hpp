#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Material/Color.hpp>
#include <Serialization/Serializable.hpp>
#include <Serialization/AssetReference.hpp>
#include <Shader/ShaderAsset.hpp>
#include <cstdint>
#include <memory>

namespace Ursa
{
enum class MaterialType
{
	Unlit,
	PbrMetallicRoughness
};
enum class AlphaMode
{
	Opaque,
	Mask,
	Blend
};
enum class TextureColorSpace
{
	Linear,
	Srgb
};
enum class MaterialTextureSlot
{
	BaseColor,
	MetallicRoughness,
	Normal,
	Occlusion,
	Emissive
};

struct MaterialSampler
{
	bool linear = true;
	bool repeat = true;
};

struct TextureMip
{
	Ursa::UInt32 width = 0;
	Ursa::UInt32 height = 0;
	Vector<Ursa::UInt8> pixels;
};

class MaterialTexture final : public Serializable
{
public:
	String SerializationType() const override
	{
		return "ursa.Texture";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	static std::shared_ptr<MaterialTexture> CreateForDeserialization();

	static Result<std::shared_ptr<const MaterialTexture>>
	Create(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight, TextureColorSpace textureColorSpace,
	       Vector<Ursa::UInt8> rgba8, Vector<TextureMip> mipmaps = {}, MaterialSampler sampler = {});

	Ursa::UInt32 GetWidth() const
	{
		return width;
	}

	Ursa::UInt32 GetHeight() const
	{
		return height;
	}

	TextureColorSpace GetColorSpace() const
	{
		return colorSpace;
	}

	const Vector<Ursa::UInt8>& GetPixels() const
	{
		return pixels;
	}

	const Vector<TextureMip>& GetMipmaps() const
	{
		return mipmaps;
	}

	const MaterialSampler& DefaultSampler() const
	{
		return sampler;
	}

	Ursa::UInt32 GetMipLevels() const
	{
		return static_cast<Ursa::UInt32>(mipmaps.size() + 1);
	}

private:
	MaterialTexture(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight, TextureColorSpace textureColorSpace,
	                Vector<Ursa::UInt8> texturePixels, Vector<TextureMip> textureMipmaps,
	                MaterialSampler textureSampler);

	Ursa::UInt32 width;
	Ursa::UInt32 height;
	TextureColorSpace colorSpace;
	Vector<Ursa::UInt8> pixels;
	Vector<TextureMip> mipmaps;
	MaterialSampler sampler;
};

using MaterialTexturePtr = std::shared_ptr<const MaterialTexture>;

struct MaterialPropertyValue
{
	Shader::PropertyType type = Shader::PropertyType::Float;
	Ursa::Float32 floatValue = 0.0f;
	Ursa::Int32 intValue = 0;
	Array<Ursa::Float32, 4> vectorValue{};
	ResourceId textureId;
	UInt64 textureLocalFileId = 0;
	String textureDefault;
	MaterialTexturePtr texture;

	static MaterialPropertyValue Float(Ursa::Float32 value);

	static MaterialPropertyValue Range(Ursa::Float32 value);

	static MaterialPropertyValue Int(Ursa::Int32 value);

	static MaterialPropertyValue ColorValue(Color value);

	static MaterialPropertyValue VectorValue(Array<Ursa::Float32, 4> value);

	static MaterialPropertyValue Texture2D(ResourceId id, MaterialTexturePtr value = {});
};

struct MaterialParameters
{
	Color baseColor{};
	Ursa::Float32 metallic = 0.0f;
	Ursa::Float32 roughness = 1.0f;
	Ursa::Float32 normalScale = 1.0f;
	Ursa::Float32 occlusionStrength = 1.0f;
	Color emissive{0.0f, 0.0f, 0.0f, 1.0f};
	AlphaMode alphaMode = AlphaMode::Opaque;
	Ursa::Float32 alphaCutoff = 0.5f;
	MaterialSampler sampler{};
};

class MaterialInstance final : public Serializable
{
public:
	String SerializationType() const override
	{
		return "ursa.Material";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	explicit MaterialInstance(MaterialType materialType);

	MaterialType Type() const
	{
		return type;
	}

	Ursa::UInt64 Version() const
	{
		return version;
	}

	const MaterialParameters& Parameters() const
	{
		return parameters;
	}

	bool SamplerOverridden() const
	{
		return samplerOverridden;
	}

	Result<void> SetBaseColor(Color color);

	Result<void> SetMetallic(Ursa::Float32 value);

	Result<void> SetRoughness(Ursa::Float32 value);

	Result<void> SetNormalScale(Ursa::Float32 value);

	Result<void> SetOcclusionStrength(Ursa::Float32 value);

	Result<void> SetEmissive(Color color);

	Result<void> SetAlphaMode(AlphaMode mode, Ursa::Float32 cutoff = 0.5f);

	Result<void> SetSampler(MaterialSampler sampler);

	Result<void> SetTexture(MaterialTextureSlot slot, MaterialTexturePtr texture);

	MaterialTexturePtr Texture(MaterialTextureSlot slot) const;

	ResourceId TextureResourceId(MaterialTextureSlot slot) const;

	AssetReference TextureReference(MaterialTextureSlot slot) const;

	Result<void> SetTextureReference(MaterialTextureSlot slot, AssetReference reference, MaterialTexturePtr texture);

	Result<void> SetTextureResourceId(MaterialTextureSlot slot, ResourceId id);

	MaterialTexturePtr ResolvedTexture(MaterialTextureSlot slot) const;

	bool IsShaderBacked() const
	{
		return static_cast<bool>(shaderId);
	}

	ResourceId ShaderId() const
	{
		return shaderId;
	}

	std::shared_ptr<const Shader::ShaderAsset> Shader() const
	{
		return shaderAsset;
	}

	Result<void> SetShader(ResourceId id);

	Result<void> SetShaderAsset(std::shared_ptr<const Shader::ShaderAsset> asset);

	Result<void> MigrateLegacyToShader(ResourceId id);

	Result<void> SetPropertyValue(const String& identifier, const Shader::ShaderAsset& shader,
	                              MaterialPropertyValue value);

	Result<void> ValidateAgainstShader(const Shader::ShaderAsset& shader) const;
	Result<void> UpgradeBuiltinPropertySchema();

	Result<MaterialPropertyValue> PropertyValue(const String& identifier, const Shader::ShaderAsset& shader) const;

	Result<void> RemovePropertyOverride(const String& identifier);

	const Map<String, MaterialPropertyValue>& PropertyOverrides() const
	{
		return propertyOverrides;
	}

	Result<void> SetKeywordEnabled(const String& keyword, bool enabled);

	bool KeywordEnabled(const String& keyword) const;

	const Vector<String>& Keywords() const
	{
		return keywords;
	}

	std::shared_ptr<MaterialInstance> Clone() const;

	bool IsValid() const;

	Result<void> Destroy();

	ResourceId LocalIdentity() const
	{
		return localIdentity;
	}

	Result<void> BindLocalIdentity(ResourceId id)
	{
		if (!id)
		{
			return Result<void>::Failure("Local resource identity cannot be zero");
		}
		localIdentity = id;
		return {};
	}

	AssetReference AssetIdentity() const;

	Result<void> BindAssetIdentity(AssetReference reference);

private:
	Result<void> DeserializeLegacy(const ArchiveReader& reader, SerializationContext& context);

	void SynchronizeBuiltinProperties();

	MaterialType type;
	bool alive = true;
	AssetReference assetIdentity;
	ResourceId localIdentity = ResourceId::Generate();
	ResourceId shaderId;
	UInt32 shaderPropertyVersion = 0;
	std::shared_ptr<const Shader::ShaderAsset> shaderAsset;
	MaterialParameters parameters;
	bool samplerOverridden = false;
	Array<MaterialTexturePtr, 5> textures{};
	Array<ResourceId, 5> textureResourceIds{};
	Array<Ursa::UInt64, 5> textureLocalFileIds{};
	Map<String, MaterialPropertyValue> propertyOverrides;
	Vector<String> keywords;
	Ursa::UInt64 version = 1;
};

MaterialTexturePtr DefaultMaterialTexture(MaterialTextureSlot slot);
} // namespace Ursa
