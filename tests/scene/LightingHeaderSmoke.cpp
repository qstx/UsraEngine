#include <scene/Scene.hpp>

int main() {
    ursa::Scene litScene;
    auto cameraNode = litScene.createNode().value;
    litScene.setActiveCamera(litScene.addComponent<ursa::CameraComponent>(cameraNode).value);
    auto sunNode = litScene.createNode().value;
    litScene.addComponent<ursa::DirectionalLightComponent>(sunNode);
    auto lampNode = litScene.createNode().value;
    litScene.addComponent<ursa::PointLightComponent>(lampNode);
    litScene.setAmbientLight({1.0f, 1.0f, 1.0f, 1.0f}, 0.1f);
    auto frame = litScene.collectRenderData(16.0f / 9.0f);
    return frame && frame.value.lighting.directional.size() == 1 && frame.value.lighting.point.size() == 1 ? 0 : 1;
}
