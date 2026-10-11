#include <Shader/ShaderReflection.hpp>

#include <cstring>
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
	shader.name = "Ursa/Reflected";
	PropertyDeclaration tint;
	tint.identifier = "_Tint";
	tint.displayName = "Tint";
	tint.type = PropertyType::Color;
	tint.defaultVector = {1, 1, 1, 1};
	shader.properties.push_back(tint);
	PropertyDeclaration roughness;
	roughness.identifier = "_Roughness";
	roughness.displayName = "Roughness";
	roughness.type = PropertyType::Range;
	roughness.rangeMinimum = 0;
	roughness.rangeMaximum = 1;
	roughness.defaultFloat = 0.5f;
	shader.properties.push_back(roughness);
	PropertyDeclaration mainTexture;
	mainTexture.identifier = "_MainTex";
	mainTexture.displayName = "Main Texture";
	mainTexture.type = PropertyType::Texture2D;
	mainTexture.defaultTexture = "white";
	shader.properties.push_back(mainTexture);
	return shader;
}

ShaderVariantAsset MakeVariant()
{
	const String vertexReflection = R"json({
"entryPoints":[{"name":"VSMain","mode":"vert"}],
"ubos":[{"name":"MaterialData","set":0,"binding":1,"block_size":32,"members":[
{"name":"_Tint","type":"vec4","offset":0,"size":16},
{"name":"_Roughness","type":"float","offset":16,"size":4}
]}]
})json";
	const String fragmentReflection = R"json({
"entryPoints":[{"name":"PSMain","mode":"frag"}],
"ubos":[{"name":"MaterialData","set":0,"binding":1,"block_size":32,"members":[
{"name":"_Tint","type":"vec4","offset":0,"size":16},
{"name":"_Roughness","type":"float","offset":16,"size":4}
]}],
"separate_images":[{"name":"_MainTex","type":"texture2d","set":0,"binding":2}],
"separate_samplers":[{"name":"sampler_MainTex","type":"sampler","set":0,"binding":3}]
})json";
	ShaderVariantAsset variant;
	variant.stages.push_back(
	    {ShaderTarget::VulkanSpirv, ProgramStage::Vertex, "VSMain", {3, 2, 35, 7}, vertexReflection});
	variant.stages.push_back(
	    {ShaderTarget::VulkanSpirv, ProgramStage::Fragment, "PSMain", {3, 2, 35, 7}, fragmentReflection});
	return variant;
}
} // namespace

int main()
{
	try
	{
		ShaderAsset shader = MakeShader();
		ShaderVariantAsset variant = MakeVariant();
		auto layout = BuildShaderBindingLayout(shader, variant);
		Expect(static_cast<bool>(layout), "Shader resource layout did not build from SPIR-V reflection");
		Expect(layout.value.descriptors.size() == 3 && layout.value.descriptors[0].binding == 1 &&
		           layout.value.descriptors[0].type == Rhi::BindingType::UniformBuffer &&
		           layout.value.descriptors[0].stages == 3,
		       "Uniform buffer layout did not merge vertex and fragment visibility");
		Expect(layout.value.properties.at("_Tint").byteOffset == 0 &&
		           layout.value.properties.at("_Roughness").byteOffset == 16 &&
		           layout.value.properties.at("_MainTex").binding == 2,
		       "Shader property bindings do not match reflected offsets and descriptors");
		Rhi::DeviceDesc deviceDescription;
#ifndef NDEBUG
		deviceDescription.validation = true;
#endif
		auto device = Rhi::CreateDevice(deviceDescription);
		Expect(static_cast<bool>(device), "Cannot initialize Vulkan to verify reflected resource layouts");
		auto rhiLayout = CreateShaderBindGroupLayout(layout.value, *device.value);
		Expect(static_cast<bool>(rhiLayout), "Reflected descriptor layout was rejected by the Vulkan backend");

		MaterialInstance material(MaterialType::Unlit);
		const ResourceId textureId = ResourceId::Generate();
		auto texture = MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {255, 255, 255, 255});
		Expect(static_cast<bool>(texture) && static_cast<bool>(material.SetShader(shader.id)) &&
		           static_cast<bool>(material.SetPropertyValue(
		               "_Tint", shader, MaterialPropertyValue::ColorValue({0.2f, 0.4f, 0.6f, 1.0f}))) &&
		           static_cast<bool>(
		               material.SetPropertyValue("_Roughness", shader, MaterialPropertyValue::Range(0.75f))) &&
		           static_cast<bool>(material.SetPropertyValue(
		               "_MainTex", shader, MaterialPropertyValue::Texture2D(textureId, texture.value))),
		       "Cannot assign Shader-backed material properties for binding test");
		auto uniformBytes = PackMaterialUniformBuffer(shader, layout.value, material, 1);
		Expect(static_cast<bool>(uniformBytes) && uniformBytes.value.size() == 32,
		       "Material constant buffer did not match reflected block size");
		Array<Float32, 4> tint{};
		Float32 roughness = 0;
		std::memcpy(tint.data(), uniformBytes.value.data(), sizeof(tint));
		std::memcpy(&roughness, uniformBytes.value.data() + 16, sizeof(roughness));
		Expect(tint == Array<Float32, 4>{0.2f, 0.4f, 0.6f, 1.0f} && roughness == 0.75f,
		       "Material property values were packed at incorrect reflected offsets");
		auto textureBindings = BuildMaterialTextureBindings(shader, layout.value, material);
		Expect(textureBindings && textureBindings.value.size() == 1 && textureBindings.value[0].binding == 2 &&
		           textureBindings.value[0].textureId == textureId,
		       "Stable texture ID did not resolve to its reflected Shader binding");

		ShaderVariantAsset missingTextureBinding = variant;
		missingTextureBinding.stages[1].reflectionJson = R"json({"entryPoints":[{"name":"PSMain","mode":"frag"}]})json";
		auto missingLayout = BuildShaderBindingLayout(shader, missingTextureBinding);
		Expect(missingLayout && !BuildMaterialTextureBindings(shader, missingLayout.value, material),
		       "Material texture with no reflected descriptor binding was accepted");

		ShaderVariantAsset badType = variant;
		badType.stages[0].reflectionJson =
		    R"json({"entryPoints":[{"name":"VSMain","mode":"vert"}],"ubos":[{"name":"MaterialData","set":0,"binding":1,"block_size":32,"members":[{"name":"_Tint","type":"float","offset":0,"size":4}]}]})json";
		Expect(!BuildShaderBindingLayout(shader, badType), "Incompatible reflected property type was accepted");

		ShaderVariantAsset invalidSet = variant;
		invalidSet.stages[0].reflectionJson =
		    R"json({"entryPoints":[{"name":"VSMain","mode":"vert"}],"ubos":[{"name":"MaterialData","set":1,"binding":1,"block_size":32,"members":[]}]})json";
		Expect(!BuildShaderBindingLayout(shader, invalidSet), "Unsupported descriptor set was accepted");
		std::cout << "Shader reflection tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
