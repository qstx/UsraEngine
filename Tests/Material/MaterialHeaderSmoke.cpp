#include <Material/Material.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/TextureResource.hpp>
#include <Shader/ShaderAsset.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <memory>

int main()
{
	auto material = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::PbrMetallicRoughness);
	auto pixels = Ursa::MaterialTexture::Create(1, 1, Ursa::TextureColorSpace::Srgb, {220, 80, 40, 255});
	if (!pixels || !material->SetTexture(Ursa::MaterialTextureSlot::BaseColor, pixels.value) ||
	    !material->SetMetallic(0.2f) || !material->SetRoughness(0.6f))
	{
		return 1;
	}
	auto independent = material->Clone();
	if (independent->Parameters().roughness != 0.6f)
	{
		return 1;
	}
	Ursa::Shader::ShaderAsset shader;
	shader.id = Ursa::ResourceId::Generate();
	Ursa::Shader::PropertyDeclaration tintProperty;
	tintProperty.identifier = "_Tint";
	tintProperty.type = Ursa::Shader::PropertyType::Color;
	tintProperty.defaultVector = {1.0f, 1.0f, 1.0f, 1.0f};
	shader.properties.push_back(tintProperty);
	Ursa::MaterialInstance shaderMaterial(Ursa::MaterialType::Unlit);
	if (!shaderMaterial.SetShader(shader.id) ||
	    !shaderMaterial.SetPropertyValue("_Tint", shader,
	                                     Ursa::MaterialPropertyValue::ColorValue({0.2f, 0.4f, 0.6f, 1.0f})) ||
	    !shaderMaterial.SetKeywordEnabled("ALPHA_TEST", true))
	{
		return 1;
	}
	Ursa::VirtualFileSystem filesystem;
	auto memory = std::make_shared<Ursa::MemoryFileProvider>();
	if (!filesystem.MountProvider("prj", memory, Ursa::MountAccess::ReadWrite))
	{
		return 1;
	}
	const auto materialId = Ursa::ResourceId::Generate();
	auto path = Ursa::VirtualPath::Parse("prj:Materials/ShaderBacked.ursamat");
	if (!path || !Ursa::SaveMaterialResource(filesystem, path.value, materialId, shaderMaterial, {}))
	{
		return 1;
	}
	auto saved = Ursa::ReadJsonResource(filesystem, path.value, Ursa::ResourceKind::Material);
	return saved && saved.value["version"] == Ursa::materialResourceFormatVersion &&
	               saved.value["shaderId"].get<std::string>() == Ursa::ToStdString(shader.id.ToString())
	           ? 0
	           : 1;
}
