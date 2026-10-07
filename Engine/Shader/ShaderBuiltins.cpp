#include <Shader/ShaderBuiltins.hpp>

#include <utility>

namespace Ursa::Shader
{
ResourceId BuiltinUnlitShaderId()
{
	static const ResourceId id = ResourceId::Parse("10000000-0000-4000-8000-000000000001").value;
	return id;
}

ResourceId BuiltinPbrShaderId()
{
	static const ResourceId id = ResourceId::Parse("10000000-0000-4000-8000-000000000002").value;
	return id;
}

ShaderAsset CreateBuiltinUnlitShaderAsset()
{
	ShaderAsset shader;
	shader.id = BuiltinUnlitShaderId();
	shader.name = "Ursa/Unlit";
	shader.sourcePath = "ursa:Shaders/Renderer3D/Unlit.ursashader";
	shader.sourceHash = "builtin-unlit-v1";
	shader.compilerVersion = "builtin";
	PropertyDeclaration baseColor;
	baseColor.identifier = "_BaseColor";
	baseColor.displayName = "Base Color";
	baseColor.type = PropertyType::Color;
	baseColor.defaultVector = {1.0f, 1.0f, 1.0f, 1.0f};
	shader.properties.push_back(std::move(baseColor));
	PropertyDeclaration alphaMode;
	alphaMode.identifier = "_AlphaMode";
	alphaMode.displayName = "Alpha Mode";
	alphaMode.type = PropertyType::Int;
	shader.properties.push_back(std::move(alphaMode));
	PropertyDeclaration alphaCutoff;
	alphaCutoff.identifier = "_AlphaCutoff";
	alphaCutoff.displayName = "Alpha Cutoff";
	alphaCutoff.type = PropertyType::Range;
	alphaCutoff.defaultFloat = 0.5f;
	alphaCutoff.rangeMinimum = 0.0f;
	alphaCutoff.rangeMaximum = 1.0f;
	shader.properties.push_back(std::move(alphaCutoff));
	PropertyDeclaration baseMap;
	baseMap.identifier = "_MainTex";
	baseMap.displayName = "Base Map";
	baseMap.type = PropertyType::Texture2D;
	baseMap.defaultTexture = "white";
	shader.properties.push_back(std::move(baseMap));
	return shader;
}

ShaderAsset CreateBuiltinPbrShaderAsset()
{
	ShaderAsset shader;
	shader.id = BuiltinPbrShaderId();
	shader.name = "Ursa/PBR";
	shader.sourcePath = "ursa:Shaders/Renderer3D/Pbr.ursashader";
	shader.sourceHash = "builtin-pbr-v1";
	shader.compilerVersion = "builtin";
	auto addScalar = [&shader](const char* identifier, const char* display, PropertyType type, Float32 value,
	                           Float32 minimum, Float32 maximum)
	{
		PropertyDeclaration property;
		property.identifier = identifier;
		property.displayName = display;
		property.type = type;
		property.defaultFloat = value;
		property.rangeMinimum = minimum;
		property.rangeMaximum = maximum;
		shader.properties.push_back(std::move(property));
	};
	auto addTexture = [&shader](const char* identifier, const char* display, const char* fallback, bool normal = false)
	{
		PropertyDeclaration property;
		property.identifier = identifier;
		property.displayName = display;
		property.type = PropertyType::Texture2D;
		property.defaultTexture = fallback;
		if (normal)
		{
			property.attributes.push_back("Normal");
		}
		shader.properties.push_back(std::move(property));
	};
	PropertyDeclaration baseColor;
	baseColor.identifier = "_BaseColor";
	baseColor.displayName = "Base Color";
	baseColor.type = PropertyType::Color;
	baseColor.defaultVector = {1.0f, 1.0f, 1.0f, 1.0f};
	shader.properties.push_back(std::move(baseColor));
	addScalar("_Metallic", "Metallic", PropertyType::Range, 0.0f, 0.0f, 1.0f);
	addScalar("_Roughness", "Roughness", PropertyType::Range, 1.0f, 0.0f, 1.0f);
	addScalar("_NormalScale", "Normal Scale", PropertyType::Float, 1.0f, 0.0f, 1.0f);
	addScalar("_OcclusionStrength", "Occlusion Strength", PropertyType::Range, 1.0f, 0.0f, 1.0f);
	PropertyDeclaration emissive;
	emissive.identifier = "_Emissive";
	emissive.displayName = "Emissive";
	emissive.type = PropertyType::Color;
	emissive.defaultVector = {0.0f, 0.0f, 0.0f, 1.0f};
	emissive.attributes.push_back("HDR");
	shader.properties.push_back(std::move(emissive));
	PropertyDeclaration alphaMode;
	alphaMode.identifier = "_AlphaMode";
	alphaMode.displayName = "Alpha Mode";
	alphaMode.type = PropertyType::Int;
	shader.properties.push_back(std::move(alphaMode));
	addScalar("_AlphaCutoff", "Alpha Cutoff", PropertyType::Range, 0.5f, 0.0f, 1.0f);
	addTexture("_BaseMap", "Base Map", "white");
	addTexture("_MetallicRoughnessMap", "Metallic Roughness", "white");
	addTexture("_NormalMap", "Normal Map", "bump", true);
	addTexture("_OcclusionMap", "Occlusion", "white");
	addTexture("_EmissiveMap", "Emissive Map", "black");
	return shader;
}
} // namespace Ursa::Shader
