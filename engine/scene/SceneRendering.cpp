#include <scene/Scene.hpp>
#include <core/StringInterop.hpp>
#include <cmath>
#include <exception>
#include <string>

namespace ursa {
Result<void> Scene::setActiveCamera(ComponentId id) {
    if (id &&
        (!containsComponent(id) || dynamic_cast<CameraComponent *>(components_[id.index].object.get()) == nullptr))
        return Result<void>::failure("Active camera must refer to a CameraComponent in this scene");
    activeCamera_ = id;
    return {};
}

Result<void> Scene::setAmbientLight(Color color, float intensity) {
    if (!validLightColor(color) || !std::isfinite(intensity) || intensity < 0.0f)
        return Result<void>::failure("Ambient light color or intensity must be finite and nonnegative");
    ambientColor_ = color;
    ambientIntensity_ = intensity;
    return {};
}

Result<SceneRenderData> Scene::collectRenderData(float aspect) const {
    if (!containsComponent(activeCamera_))
        return Result<SceneRenderData>::failure("Scene has no active camera");
    const ComponentSlot &cameraSlot = components_[activeCamera_.index];
    const auto *camera = dynamic_cast<const CameraComponent *>(cameraSlot.object.get());
    if (!camera || !isComponentActive(activeCamera_).value)
        return Result<SceneRenderData>::failure("Scene active camera is disabled or invalid");
    try {
        const Mat4 projection =
            Mat4::perspective(camera->verticalFovRadians, aspect, camera->nearPlane, camera->farPlane);
        const Mat4 viewProjection = projection * computeWorld(cameraSlot.node).inverseAffine();
        const Mat4 cameraWorld = computeWorld(cameraSlot.node);
        return collectWithCamera(viewProjection,
                                 {cameraWorld.at(0, 3), cameraWorld.at(1, 3), cameraWorld.at(2, 3)});
    } catch (const std::exception &error) {
        return Result<SceneRenderData>::failure(String("Cannot collect scene rendering data: ") + error.what());
    }
}

Result<SceneRenderData> Scene::collectPreviewRenderData(const Camera &camera, float aspect) const {
    try {
        return collectWithCamera(camera.viewProjection(aspect), camera.position);
    } catch (const std::exception &error) {
        return Result<SceneRenderData>::failure(String("Cannot collect scene preview: ") + error.what());
    }
}

Result<SceneRenderData> Scene::collectWithCamera(const Mat4 &viewProjection, Vec3 cameraPosition) const {
    try {
        SceneRenderData data;
        data.viewProjection = viewProjection;
        data.cameraPosition = cameraPosition;
        data.lighting.ambientColor = ambientColor_;
        data.lighting.ambientIntensity = ambientIntensity_;
        for (NodeId root : rootNodes())
            collectRendering(root, data);
        if (data.lighting.point.size() > SceneLighting::maxPointLights)
            return Result<SceneRenderData>::failure(
                toUrsaString("Active point light count " + std::to_string(data.lighting.point.size()) +
                             " exceeds renderer capacity " + std::to_string(SceneLighting::maxPointLights)));
        if (data.lighting.directional.size() > SceneLighting::maxDirectionalLights)
            return Result<SceneRenderData>::failure(
                toUrsaString("Active directional light count " + std::to_string(data.lighting.directional.size()) +
                             " exceeds renderer capacity " + std::to_string(SceneLighting::maxDirectionalLights)));
        return Result<SceneRenderData>::success(std::move(data));
    } catch (const std::exception &error) {
        return Result<SceneRenderData>::failure(String("Cannot collect scene rendering data: ") + error.what());
    }
}

void Scene::collectRendering(NodeId node, SceneRenderData &out) const {
    if (!isActive(node).value)
        return;
    const NodeSlot &slot = nodes_[node.index];
    for (ComponentId id : slot.components) {
        if (!containsComponent(id) || !isComponentActive(id).value)
            continue;
        const auto &object = components_[id.index].object;
        if (const auto *box = dynamic_cast<const BoxRenderComponent *>(object.get()))
            out.boxes.push_back({computeWorld(node), box->color, box->material});
        if (const auto *light = dynamic_cast<const DirectionalLightComponent *>(object.get())) {
            const Mat4 world = computeWorld(node);
            out.lighting.directional.push_back(
                {normalized({-world.at(0, 2), -world.at(1, 2), -world.at(2, 2)}), light->color(), light->intensity()});
        }
        if (const auto *light = dynamic_cast<const PointLightComponent *>(object.get())) {
            const Mat4 world = computeWorld(node);
            out.lighting.point.push_back(
                {{world.at(0, 3), world.at(1, 3), world.at(2, 3)}, light->color(), light->intensity(), light->range()});
        }
    }
    for (NodeId child : slot.children)
        collectRendering(child, out);
}
} // namespace ursa
