#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <renderer3d/Renderer3D.hpp>
#include <scene/Scene.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template <class T> T take(ursa::Result<T> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
    return std::move(result.value);
}

void done(ursa::Result<void> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
}

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

ursa::Vector<uint8_t> readFrame(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer,
                                const ursa::Vector<ursa::ModelBoxInstance> &boxes,
                                const ursa::SceneLighting &lighting = {}, uint32_t width = 64, uint32_t height = 64) {
    using namespace ursa::rhi;
    ursa::Camera camera;
    camera.position = {0, 0, 5};
    camera.target = {};
    auto color = take(device->createTexture(
        {width, height, Format::RGBA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
    done(take(renderer.renderOffscreenModels(color, camera.viewProjection(float(width) / height), boxes, lighting,
                                             camera.position))
             ->wait());
    auto readback = take(device->createBuffer(
        {static_cast<size_t>(width) * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
    auto command = take(device->createCommandList());
    done(command->begin());
    done(command->barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
    done(command->copyTextureToBuffer(color, readback));
    done(command->end());
    done(take(device->submit(command))->wait());
    ursa::Vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    done(readback->read(0, pixels.data(), pixels.size()));
    return pixels;
}

ursa::Array<uint8_t, 4> pixel(const ursa::Vector<uint8_t> &image, uint32_t x, uint32_t y, uint32_t width = 64) {
    const size_t index = (static_cast<size_t>(y) * width + x) * 4;
    return {image[index], image[index + 1], image[index + 2], image[index + 3]};
}

ursa::ModelBoxInstance box(ursa::Vec3 position, ursa::Vec3 scale, std::shared_ptr<ursa::MaterialInstance> material = {},
                           ursa::Color color = {}) {
    return {ursa::Mat4::translation(position) * ursa::Mat4::scale(scale), color, std::move(material)};
}

ursa::MaterialTexturePtr texture(uint32_t width, uint32_t height, ursa::TextureColorSpace space,
                                 ursa::Vector<uint8_t> pixels) {
    return take(ursa::MaterialTexture::create(width, height, space, std::move(pixels)));
}

std::shared_ptr<ursa::MaterialInstance> material(ursa::MaterialType type, ursa::Color color = {}) {
    auto result = std::make_shared<ursa::MaterialInstance>(type);
    done(result->setBaseColor(color));
    return result;
}

ursa::SceneLighting sun(float intensity = 4.0f) {
    ursa::SceneLighting lighting;
    lighting.ambientIntensity = 0.0f;
    lighting.directional.push_back({{0, 0, -1}, {}, intensity});
    return lighting;
}

int difference(ursa::Array<uint8_t, 4> a, ursa::Array<uint8_t, 4> b) {
    return std::abs(int(a[0]) - int(b[0])) + std::abs(int(a[1]) - int(b[1])) + std::abs(int(a[2]) - int(b[2]));
}

void testUvAndColorSpace(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer) {
    auto unlit = material(ursa::MaterialType::Unlit);
    done(unlit->setSampler({false, false}));
    done(unlit->setTexture(ursa::MaterialTextureSlot::BaseColor,
                           texture(2, 1, ursa::TextureColorSpace::Srgb, {255, 0, 0, 255, 0, 0, 255, 255})));
    const auto image = readFrame(device, renderer, {box({}, {2, 2, 1}, unlit)});
    const auto left = pixel(image, 20, 32);
    const auto right = pixel(image, 44, 32);
    expect(left[0] > 240 && left[2] < 20 && right[2] > 240 && right[0] < 20,
           "Cube UVs or procedural base-color sampling failed");
    done(unlit->setTexture(ursa::MaterialTextureSlot::BaseColor,
                           texture(1, 1, ursa::TextureColorSpace::Srgb, {128, 128, 128, 255})));
    const auto gray = pixel(readFrame(device, renderer, {box({}, {1, 1, 1}, unlit)}), 32, 32);
    expect(gray[0] >= 123 && gray[0] <= 133, "sRGB material texture was not decoded and encoded correctly");
}

void testPbrMapsAndLighting(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer) {
    auto pbr = material(ursa::MaterialType::PbrMetallicRoughness, {0.8f, 0.3f, 0.15f, 1});
    auto render = [&](const ursa::SceneLighting &lighting) {
        return pixel(readFrame(device, renderer, {box({}, {1.5f, 1.5f, 1}, pbr)}, lighting), 32, 32);
    };
    const auto directional = sun();
    const auto original = render(directional);
    done(pbr->setBaseColor({0.15f, 0.3f, 0.8f, 1}));
    const auto blue = render(directional);
    expect(original[0] > original[2] && blue[2] > blue[0], "PBR base color had no visible effect");
    done(pbr->setMetallic(1.0f));
    done(pbr->setRoughness(0.2f));
    const auto metal = render(directional);
    expect(difference(blue, metal) > 15, "PBR metallic or roughness factors had no visible effect");
    done(pbr->setTexture(ursa::MaterialTextureSlot::MetallicRoughness,
                         texture(1, 1, ursa::TextureColorSpace::Linear, {0, 255, 0, 255})));
    const auto mappedMetal = render(directional);
    expect(difference(metal, mappedMetal) > 10, "Metallic-roughness texture had no visible effect");
    done(pbr->setTexture(ursa::MaterialTextureSlot::MetallicRoughness, {}));
    auto angled = directional;
    angled.directional[0].direction = {-0.55f, 0, -0.84f};
    const auto flatNormal = render(angled);
    done(pbr->setTexture(ursa::MaterialTextureSlot::Normal,
                         texture(1, 1, ursa::TextureColorSpace::Linear, {220, 128, 220, 255})));
    const auto tiltedNormal = render(angled);
    expect(difference(flatNormal, tiltedNormal) > 10, "Tangent-space normal map had no visible effect");
    done(pbr->setTexture(ursa::MaterialTextureSlot::Normal, {}));
    ursa::SceneLighting ambient;
    ambient.ambientIntensity = 1.0f;
    const auto unoccluded = render(ambient);
    done(pbr->setTexture(ursa::MaterialTextureSlot::Occlusion,
                         texture(1, 1, ursa::TextureColorSpace::Linear, {0, 0, 0, 255})));
    const auto occluded = render(ambient);
    expect(difference(unoccluded, occluded) > 15, "Occlusion map did not affect ambient light");
    done(pbr->setEmissive({1.0f, 0.0f, 0.0f, 1.0f}));
    const auto emissive = render(ambient);
    expect(emissive[0] > occluded[0] + 80, "Emissive factor had no visible effect");
    done(pbr->setTexture(ursa::MaterialTextureSlot::Emissive,
                         texture(1, 1, ursa::TextureColorSpace::Srgb, {0, 0, 0, 255})));
    expect(render(ambient)[0] < emissive[0] - 80, "Emissive texture had no visible effect");

    auto unlit = material(ursa::MaterialType::Unlit, {0.4f, 0.5f, 0.6f, 1});
    const auto unlitBox = box({}, {1, 1, 1}, unlit);
    const auto withSun = pixel(readFrame(device, renderer, {unlitBox}, directional), 32, 32);
    const auto withoutSun = pixel(readFrame(device, renderer, {unlitBox}, ambient), 32, 32);
    expect(withSun == withoutSun, "Unlit material changed with lighting");

    done(pbr->setEmissive({0, 0, 0, 1}));
    done(pbr->setTexture(ursa::MaterialTextureSlot::Emissive, {}));
    done(pbr->setTexture(ursa::MaterialTextureSlot::Occlusion, {}));
    done(pbr->setMetallic(0.0f));
    done(pbr->setRoughness(1.0f));
    const auto noLight = render(sun(0));
    const auto direct = render(directional);
    expect(difference(noLight, direct) > 15, "Directional light did not affect PBR");
    const auto side = render(angled);
    expect(difference(direct, side) > 5, "Directional orientation did not affect PBR");
    ursa::SceneLighting point;
    point.ambientIntensity = 0;
    point.point.push_back({{0, 0, 2}, {}, 20.0f, 5.0f});
    const auto near = render(point);
    point.point[0].position = {0, 0, 4};
    const auto far = render(point);
    expect(difference(near, far) > 10, "Point-light distance had no effect");
    point.point[0].position = {0, 0, 2};
    point.point.push_back({{1, 0, 2}, {}, 20.0f, 5.0f});
    const auto twoPoints = render(point);
    expect(difference(near, twoPoints) > 5, "Second point light was ignored");
}

void testTransformedNormals(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer) {
    auto pbr = material(ursa::MaterialType::PbrMetallicRoughness, {0.8f, 0.8f, 0.8f, 1});
    auto cube = box({}, {1.0f, 0.7f, 1.0f}, pbr);
    cube.model = ursa::Mat4::rotation(ursa::Quat::fromAxisAngle({0, 1, 0}, 0.78539816f)) * cube.model;
    const auto unscaled = cube;
    cube.model = ursa::Mat4::scale({2.0f, 1.0f, 0.5f}) * cube.model;
    auto light = sun();
    const auto baseline = pixel(readFrame(device, renderer, {unscaled}, light), 32, 32);
    const auto flat = pixel(readFrame(device, renderer, {cube}, light), 32, 32);
    expect(flat[0] > baseline[0], "Nonuniform parent scale did not use inverse-transpose normals");
    light.directional[0].direction = {-0.4f, 0, -0.9f};
    done(pbr->setTexture(ursa::MaterialTextureSlot::Normal,
                         texture(1, 1, ursa::TextureColorSpace::Linear, {220, 128, 220, 255})));
    const auto mapped = pixel(readFrame(device, renderer, {cube}, light), 32, 32);
    expect(difference(flat, mapped) > 10, "Rotated, nonuniformly scaled cube ignored its normal map");
}

void testSharedAndLegacy(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer) {
    auto shared = material(ursa::MaterialType::Unlit, {1, 0, 0, 1});
    ursa::Vector<ursa::ModelBoxInstance> boxes{
        box({-0.8f, 0, 0}, {0.8f, 0.8f, 0.8f}, shared),
        box({0.8f, 0, 0}, {0.8f, 0.8f, 0.8f}, shared),
    };
    const auto red = readFrame(device, renderer, boxes);
    expect(pixel(red, 20, 32)[0] > 200 && pixel(red, 44, 32)[0] > 200, "Shared material was not used by both cubes");
    done(shared->setBaseColor({0, 0, 1, 1}));
    const auto blue = readFrame(device, renderer, boxes);
    expect(pixel(blue, 20, 32)[2] > 200 && pixel(blue, 44, 32)[2] > 200,
           "Shared material update did not reach the next frame");
    auto independent = shared->clone();
    done(independent->setBaseColor({0, 1, 0, 1}));
    boxes[1].material = independent;
    boxes.push_back(box({0, -1.1f, -0.2f}, {0.8f, 0.8f, 0.8f}, {}, {1, 0, 0, 1}));
    const auto mixed = readFrame(device, renderer, boxes);
    expect(pixel(mixed, 20, 32)[2] > 200 && pixel(mixed, 44, 32)[1] > 200 && pixel(mixed, 32, 50)[0] > 150,
           "Material replacement, clone, or legacy color path failed");
}

class SwapMaterialOnUpdate final : public ursa::Component {
public:
    SwapMaterialOnUpdate(ursa::BoxRenderComponent *box, std::shared_ptr<ursa::MaterialInstance> next)
        : box_(box), next_(std::move(next)) {
    }

    ursa::Result<void> onUpdate(const ursa::FrameContext &) override {
        box_->material = next_;
        return {};
    }

private:
    ursa::BoxRenderComponent *box_;
    std::shared_ptr<ursa::MaterialInstance> next_;
};

void testSceneMaterialReplacement(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer) {
    ursa::Scene scene;
    const auto cameraNode = take(scene.createNode());
    ursa::LocalTransform pose;
    pose.position = {0, 0, 5};
    done(scene.setLocalTransform(cameraNode, pose));
    done(scene.setActiveCamera(take(scene.addComponent<ursa::CameraComponent>(cameraNode))));
    const auto node = take(scene.createNode());
    take(scene.addComponent<ursa::BoxRenderComponent>(node, ursa::Color{1, 0, 0, 1}));
    auto *boxComponent = scene.findComponent<ursa::BoxRenderComponent>(node);
    const auto legacyData = take(scene.collectRenderData(1.0f));
    const auto legacy = pixel(readFrame(device, renderer, legacyData.boxes), 32, 32);
    expect(legacy[0] > 200, "Legacy scene box lost its color");
    auto blue = material(ursa::MaterialType::Unlit, {0, 0, 1, 1});
    take(scene.addComponent<SwapMaterialOnUpdate>(node, boxComponent, blue));
    scene.beginFrame();
    done(scene.update({}));
    const auto changedData = take(scene.collectRenderData(1.0f));
    const auto changed = pixel(readFrame(device, renderer, changedData.boxes), 32, 32);
    expect(changed[2] > 200 && changed[0] < 20 && !legacyData.boxes[0].material,
           "Material replacement during update missed the current render frame");
}

void testAlphaModes(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer) {
    auto red = material(ursa::MaterialType::Unlit, {1, 0, 0, 0.25f});
    auto blue = material(ursa::MaterialType::Unlit, {0, 0, 1, 1});
    const auto back = box({0, 0, -1}, {2, 2, 0.5f}, blue);
    const auto front = box({0, 0, 0}, {2, 2, 0.5f}, red);
    auto opaque = pixel(readFrame(device, renderer, {back, front}), 32, 32);
    expect(opaque[0] > 240 && opaque[2] < 20, "Opaque mode used base-color alpha");
    const auto reverseOpaque = pixel(readFrame(device, renderer, {front, back}), 32, 32);
    expect(reverseOpaque[0] > 240 && reverseOpaque[2] < 20,
           "Opaque mode did not write depth independently of input order");
    done(red->setAlphaMode(ursa::AlphaMode::Mask, 0.5f));
    done(red->setSampler({false, false}));
    done(red->setTexture(
        ursa::MaterialTextureSlot::BaseColor,
        texture(3, 1, ursa::TextureColorSpace::Srgb, {255, 255, 255, 0, 255, 255, 255, 255, 255, 255, 255, 0})));
    done(red->setBaseColor({1, 0, 0, 1}));
    const auto masked = readFrame(device, renderer, {front, back});
    expect(pixel(masked, 20, 32)[2] > 240 && pixel(masked, 32, 32)[0] > 240,
           "Mask cutoff did not discard low-alpha texels or preserve high-alpha texels");
    done(red->setAlphaMode(ursa::AlphaMode::Mask, 128.0f / 255.0f));
    done(red->setTexture(ursa::MaterialTextureSlot::BaseColor,
                         texture(1, 1, ursa::TextureColorSpace::Srgb, {255, 255, 255, 128})));
    expect(pixel(readFrame(device, renderer, {front, back}), 32, 32)[0] > 240,
           "A mask texel at the cutoff was incorrectly discarded");
    done(red->setTexture(ursa::MaterialTextureSlot::BaseColor,
                         texture(1, 1, ursa::TextureColorSpace::Srgb, {255, 255, 255, 127})));
    expect(pixel(readFrame(device, renderer, {front, back}), 32, 32)[2] > 240,
           "A mask texel below the cutoff was not discarded");
    done(red->setTexture(ursa::MaterialTextureSlot::BaseColor, {}));
    done(red->setBaseColor({1, 0, 0, 0.5f}));
    done(red->setAlphaMode(ursa::AlphaMode::Blend));
    const auto blended = pixel(readFrame(device, renderer, {front, back}), 32, 32);
    expect(blended[0] > 100 && blended[2] > 100 && blended[0] < 200, "Blend mode did not mix with opaque background");
    auto far = material(ursa::MaterialType::Unlit, {0, 1, 0, 0.5f});
    done(far->setAlphaMode(ursa::AlphaMode::Blend));
    const auto farBox = box({0, 0, -0.6f}, {2, 2, 0.5f}, far);
    const auto orderA = pixel(readFrame(device, renderer, {front, farBox, back}), 32, 32);
    const auto orderB = pixel(readFrame(device, renderer, {back, farBox, front}), 32, 32);
    expect(orderA == orderB && orderA[0] > orderA[1],
           "Blend objects were not drawn in a deterministic far-to-near order");
    const auto middleOpaque = box({0, 0, -0.3f}, {2, 2, 0.2f}, blue);
    const auto occludedFar = pixel(readFrame(device, renderer, {front, farBox, middleOpaque}), 32, 32);
    expect(occludedFar[1] < 10 && occludedFar[0] > 100 && occludedFar[2] > 100,
           "Opaque depth did not reject a farther blend object");
    const auto sameDepth = box({0, 0, 0}, {2, 2, 0.5f}, far);
    const auto equalA = pixel(readFrame(device, renderer, {front, sameDepth}), 32, 32);
    const auto equalB = pixel(readFrame(device, renderer, {front, sameDepth}), 32, 32);
    expect(equalA == equalB, "Equal-distance blend order was not stable");
}
} // namespace

int main() {
    try {
        ursa::rhi::DeviceDesc desc;
#ifndef NDEBUG
        desc.validation = true;
#endif
        auto device = take(ursa::rhi::createDevice(desc));
        auto renderer = take(ursa::Renderer3D::create(device));
        testUvAndColorSpace(device, *renderer);
        testPbrMapsAndLighting(device, *renderer);
        testTransformedNormals(device, *renderer);
        testSharedAndLegacy(device, *renderer);
        testSceneMaterialReplacement(device, *renderer);
        testAlphaModes(device, *renderer);
        renderer.reset();
        done(device->waitIdle());
        std::cout << "Material renderer GPU tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Material renderer GPU test failed: " << error.what() << '\n';
        return 1;
    }
}
