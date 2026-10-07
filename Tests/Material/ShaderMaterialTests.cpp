#include <Material/Material.hpp>

#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Shader;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

ShaderAsset MakeShader()
{
	ShaderAsset shader;
	shader.id = ResourceId::Generate();
	shader.name = "Ursa/TestMaterial";
	Shader::PropertyDeclaration range;
	range.identifier = "_Roughness";
	range.displayName = "Roughness";
	range.type = PropertyType::Range;
	range.rangeMinimum = 0.04f;
	range.rangeMaximum = 1.0f;
	range.defaultFloat = 0.5f;
	shader.properties.push_back(range);
	Shader::PropertyDeclaration color;
	color.identifier = "_Tint";
	color.displayName = "Tint";
	color.type = PropertyType::Color;
	color.defaultVector = {1.0f, 1.0f, 1.0f, 1.0f};
	shader.properties.push_back(color);
	Shader::PropertyDeclaration vector;
	vector.identifier = "_Parameters";
	vector.displayName = "Parameters";
	vector.type = PropertyType::Vector;
	vector.defaultVector = {0.0f, 0.0f, 0.0f, 0.0f};
	shader.properties.push_back(vector);
	Shader::PropertyDeclaration texture;
	texture.identifier = "_MainTex";
	texture.displayName = "Base Map";
	texture.type = PropertyType::Texture2D;
	texture.defaultTexture = "white";
	shader.properties.push_back(texture);
	Shader::PropertyDeclaration normal;
	normal.identifier = "_NormalMap";
	normal.displayName = "Normal Map";
	normal.type = PropertyType::Texture2D;
	normal.attributes.push_back("Normal");
	shader.properties.push_back(normal);
	return shader;
}
} // namespace

int main()
{
	try
	{
		const ShaderAsset shader = MakeShader();
		auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		Expect(static_cast<bool>(material->SetShader(shader.id)) && material->IsShaderBacked(),
		       "Material did not retain its Shader identity");
		auto defaultRoughness = material->PropertyValue("_Roughness", shader);
		Expect(defaultRoughness && defaultRoughness.value.floatValue == 0.5f,
		       "Shader default was not resolved for a material property");

		Expect(static_cast<bool>(material->SetPropertyValue("_Roughness", shader, MaterialPropertyValue::Range(0.25f))),
		       "Valid ranged material property was rejected");
		Expect(!material->SetPropertyValue("_Roughness", shader, MaterialPropertyValue::Range(2.0f)),
		       "Out-of-range material value was accepted");
		Expect(!material->SetPropertyValue("_Roughness", shader, MaterialPropertyValue::Float(0.25f)),
		       "Material property type mismatch was accepted");
		Expect(!material->SetPropertyValue("_Missing", shader, MaterialPropertyValue::Float(0.25f)),
		       "Undeclared Shader property was accepted");

		Expect(static_cast<bool>(material->SetPropertyValue(
		           "_Tint", shader, MaterialPropertyValue::ColorValue({0.2f, 0.3f, 0.4f, 1.0f}))),
		       "Valid Color override was rejected");
		Expect(
		    !material->SetPropertyValue("_Tint", shader, MaterialPropertyValue::ColorValue({-0.2f, 0.3f, 0.4f, 1.0f})),
		    "Out-of-range Color override was accepted");

		auto srgbTexture = MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {255, 255, 255, 255});
		auto linearTexture = MaterialTexture::Create(1, 1, TextureColorSpace::Linear, {128, 128, 255, 255});
		Expect(srgbTexture && linearTexture, "Texture fixtures could not be created");
		Expect(static_cast<bool>(material->SetPropertyValue(
		           "_MainTex", shader, MaterialPropertyValue::Texture2D(ResourceId::Generate(), srgbTexture.value))),
		       "Stable 2D texture reference was not accepted");
		Expect(!material->SetPropertyValue("_NormalMap", shader,
		                                   MaterialPropertyValue::Texture2D(ResourceId::Generate(), srgbTexture.value)),
		       "Normal map accepted an sRGB texture");
		Expect(
		    static_cast<bool>(material->SetPropertyValue(
		        "_NormalMap", shader, MaterialPropertyValue::Texture2D(ResourceId::Generate(), linearTexture.value))),
		    "Linear normal map texture was rejected");

		Expect(static_cast<bool>(material->SetKeywordEnabled("NORMAL_MAP", true)) &&
		           material->KeywordEnabled("NORMAL_MAP"),
		       "Material keyword could not be enabled");
		Expect(!material->SetKeywordEnabled("bad keyword", true), "Invalid material keyword was accepted");
		auto clone = material->Clone();
		Expect(static_cast<bool>(clone->SetPropertyValue("_Roughness", shader, MaterialPropertyValue::Range(0.75f))),
		       "Cloned material property could not be edited");
		auto originalValue = material->PropertyValue("_Roughness", shader);
		auto cloneValue = clone->PropertyValue("_Roughness", shader);
		Expect(originalValue && cloneValue && originalValue.value.floatValue == 0.25f &&
		           cloneValue.value.floatValue == 0.75f,
		       "Cloned material did not retain independent property overrides");
		auto originalTexture = material->PropertyValue("_MainTex", shader);
		Expect(originalTexture && originalTexture.value.textureId && originalTexture.value.texture == srgbTexture.value,
		       "Material texture override did not retain its stable ID and loaded resource");

		std::cout << "Shader material tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
