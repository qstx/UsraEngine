#pragma once
#include <core/Containers.hpp>
#include <renderer3d/Renderer3D.hpp>
#include <scene/Component.hpp>
#include <scene/Lighting.hpp>

namespace ursa {
class CameraComponent final : public Component {
public:
    float verticalFovRadians = 0.78539816f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

class BoxRenderComponent final : public Component {
public:
    explicit BoxRenderComponent(Color selectedColor = {}) : color(selectedColor) {
    }

    Color color;
    std::shared_ptr<MaterialInstance> material;
};

struct SceneRenderData {
    Mat4 viewProjection;
    Vec3 cameraPosition{};
    SceneLighting lighting;
    Vector<ModelBoxInstance> boxes;
};
} // namespace ursa
