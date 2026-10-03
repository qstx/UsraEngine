#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <scene/Scene.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
template <class T> T take(ursa::Result<T> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
    return result.value;
}

void done(ursa::Result<void> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
}

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void testValidation() {
    ursa::Scene scene;
    done(scene.setAmbientLight({0.3f, 0.4f, 0.5f, 1.0f}, 0.2f));
    expect(!scene.setAmbientLight({-1.0f, 0.0f, 0.0f, 1.0f}, 0.5f) && !scene.setAmbientLight({}, -1.0f) &&
               scene.ambientLightIntensity() == 0.2f && scene.ambientLightColor().red == 0.3f,
           "Invalid ambient light changed the last valid setting");
    const auto directionalNode = take(scene.createNode());
    take(scene.addComponent<ursa::DirectionalLightComponent>(directionalNode));
    auto *directional = scene.findComponent<ursa::DirectionalLightComponent>(directionalNode);
    done(directional->setColor({0.8f, 0.6f, 0.2f, 1.0f}));
    done(directional->setIntensity(2.0f));
    const auto badDirectional = directional->setIntensity(-1.0f);
    expect(!badDirectional && badDirectional.error.find("intensity") != ursa::String::npos &&
               directional->intensity() == 2.0f,
           "Invalid directional intensity changed the light");
    const auto pointNode = take(scene.createNode());
    take(scene.addComponent<ursa::PointLightComponent>(pointNode));
    auto *point = scene.findComponent<ursa::PointLightComponent>(pointNode);
    done(point->setRange(4.0f));
    done(point->setIntensity(3.0f));
    expect(!point->setRange(0.0f) && !point->setIntensity(-0.5f) && point->range() == 4.0f &&
               point->intensity() == 3.0f,
           "Invalid point light settings changed the light");
}

void testWorldSnapshotAndCapacity() {
    ursa::Scene scene;
    const auto cameraNode = take(scene.createNode());
    const auto cameraId = take(scene.addComponent<ursa::CameraComponent>(cameraNode));
    done(scene.setActiveCamera(cameraId));
    const auto parent = take(scene.createNode());
    const auto directionalNode = take(scene.createNode(parent));
    take(scene.addComponent<ursa::DirectionalLightComponent>(directionalNode));
    const auto pointNode = take(scene.createNode(parent));
    take(scene.addComponent<ursa::PointLightComponent>(pointNode));
    ursa::LocalTransform parentTransform;
    parentTransform.position = {2.0f, 0.0f, 0.0f};
    parentTransform.rotation = ursa::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 1.57079633f);
    done(scene.setLocalTransform(parent, parentTransform));
    ursa::LocalTransform pointTransform;
    pointTransform.position = {0.0f, 0.0f, -1.0f};
    done(scene.setLocalTransform(pointNode, pointTransform));
    const auto frame = take(scene.collectRenderData(1.0f));
    expect(frame.lighting.directional.size() == 1 && frame.lighting.point.size() == 1 &&
               std::abs(frame.lighting.directional[0].direction.x + 1.0f) < 0.0001f &&
               std::abs(frame.lighting.point[0].position.x - 1.0f) < 0.0001f,
           "Scene lights did not use world transforms");
    pointTransform.position = {0.0f, 0.0f, -2.0f};
    done(scene.setLocalTransform(pointNode, pointTransform));
    const auto movedFrame = take(scene.collectRenderData(1.0f));
    expect(std::abs(frame.lighting.point[0].position.x - 1.0f) < 0.0001f &&
               std::abs(movedFrame.lighting.point[0].position.x) < 0.0001f,
           "A collected frame did not retain its own light snapshot");
    done(scene.setEnabled(parent, false));
    expect(take(scene.collectRenderData(1.0f)).lighting.point.empty() &&
               take(scene.collectRenderData(1.0f)).lighting.directional.empty(),
           "Disabled ancestor still contributed lights");
    done(scene.setEnabled(parent, true));
    expect(take(scene.collectRenderData(1.0f)).lighting.point.size() == 1, "Reenabled ancestor did not restore lights");
    for (uint32_t index = 1; index < ursa::SceneLighting::maxPointLights; ++index) {
        const auto node = take(scene.createNode());
        take(scene.addComponent<ursa::PointLightComponent>(node));
    }
    expect(take(scene.collectRenderData(1.0f)).lighting.point.size() == ursa::SceneLighting::maxPointLights,
           "Point light capacity boundary failed");
    const auto extra = take(scene.createNode());
    take(scene.addComponent<ursa::PointLightComponent>(extra));
    const auto overflow = scene.collectRenderData(1.0f);
    expect(!overflow && overflow.error.find("9") != ursa::String::npos &&
               overflow.error.find("8") != ursa::String::npos,
           "Point light overflow lacked counts and capacity");
}
} // namespace

int main() {
    try {
        testValidation();
        testWorldSnapshotAndCapacity();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
