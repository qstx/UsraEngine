#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Material/Color.hpp>
#include <Serialization/Serializable.hpp>
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

	static Result<std::shared_ptr<const MaterialTexture>> Create(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight,
	                                                             TextureColorSpace textureColorSpace,
	                                                             Vector<Ursa::UInt8> rgba8);

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

private:
	MaterialTexture(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight, TextureColorSpace textureColorSpace,
	                Vector<Ursa::UInt8> texturePixels);

	Ursa::UInt32 width;
	Ursa::UInt32 height;
	TextureColorSpace colorSpace;
	Vector<Ursa::UInt8> pixels;
};

using MaterialTexturePtr = std::shared_ptr<const MaterialTexture>;

struct MaterialPropertyValue
{
	Shader::PropertyType type = Shader::PropertyType::Float;
	Ursa::Float32 floatValue = 0.0f;
	Ursa::Int32 intValue = 0;
	Array<Ursa::Float32, 4> vectorValue{};
	ResourceId textureId;
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

private:
	Result<void> DeserializeLegacy(const ArchiveReader& reader, SerializationContext& context);

	void SynchronizeBuiltinProperties();

	MaterialType type;
	ResourceId shaderId;
	std::shared_ptr<const Shader::ShaderAsset> shaderAsset;
	MaterialParameters parameters;
	Array<MaterialTexturePtr, 5> textures{};
	Array<ResourceId, 5> textureResourceIds{};
	Map<String, MaterialPropertyValue> propertyOverrides;
	Vector<String> keywords;
	Ursa::UInt64 version = 1;
};

MaterialTexturePtr DefaultMaterialTexture(MaterialTextureSlot slot);
} // namespace Ursa
