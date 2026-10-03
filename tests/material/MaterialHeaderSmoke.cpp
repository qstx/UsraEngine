#include <material/Material.hpp>
#include <memory>

int main()
{
	auto material = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::pbrMetallicRoughness);
	auto pixels = Ursa::MaterialTexture::Create(1, 1, Ursa::TextureColorSpace::srgb, {220, 80, 40, 255});
	if (!pixels || !material->SetTexture(Ursa::MaterialTextureSlot::baseColor, pixels.value) ||
	    !material->SetMetallic(0.2f) || !material->SetRoughness(0.6f))
	{
		return 1;
	}
	auto independent = material->Clone();
	return independent->Parameters().roughness == 0.6f ? 0 : 1;
}
