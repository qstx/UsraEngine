#include <material/Material.hpp>
#include <memory>

int main() {
    auto material = std::make_shared<ursa::MaterialInstance>(ursa::MaterialType::PbrMetallicRoughness);
    auto pixels = ursa::MaterialTexture::create(1, 1, ursa::TextureColorSpace::Srgb, {220, 80, 40, 255});
    if (!pixels || !material->setTexture(ursa::MaterialTextureSlot::BaseColor, pixels.value) ||
        !material->setMetallic(0.2f) || !material->setRoughness(0.6f))
        return 1;
    auto independent = material->clone();
    return independent->parameters().roughness == 0.6f ? 0 : 1;
}
