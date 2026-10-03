#include <serialization/JsonInterop.hpp>
#include <serialization/ComponentRegistry.hpp>
#include <cmath>
#include <limits>

namespace ursa {
namespace {
using Json = nlohmann::json;

Json colorJson(Color color) {
    return Json::array({color.red, color.green, color.blue, color.alpha});
}

Result<float> number(const Json &data, const char *field) {
    if (!data.is_object() || !data.contains(field) || !data[field].is_number())
        return Result<float>::failure(String("Missing or invalid component field ") + field);
    const double value = data[field].get<double>();
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        return Result<float>::failure(String("Out-of-range component field ") + field);
    return Result<float>::success(static_cast<float>(value));
}

Result<Color> color(const Json &data, const char *field) {
    if (!data.is_object() || !data.contains(field) || !data[field].is_array() || data[field].size() != 4)
        return Result<Color>::failure(String("Missing or invalid component field ") + field);
    Color value;
    float *channels[] = {&value.red, &value.green, &value.blue, &value.alpha};
    for (size_t index = 0; index < 4; ++index) {
        if (!data[field][index].is_number())
            return Result<Color>::failure(String("Invalid component color field ") + field);
        const double channel = data[field][index].get<double>();
        if (!std::isfinite(channel) || std::abs(channel) > std::numeric_limits<float>::max())
            return Result<Color>::failure(String("Out-of-range component color field ") + field);
        *channels[index] = static_cast<float>(channel);
    }
    return Result<Color>::success(value);
}

Result<void> registerCamera(ComponentRegistry &registry) {
    ComponentCodec codec;
    codec.type = "ursa.Camera";
    codec.matches = [](const Component &value) { return dynamic_cast<const CameraComponent *>(&value) != nullptr; };
    codec.save = [](const Component &value, const ComponentSaveContext &) -> Result<Json> {
        const auto &camera = static_cast<const CameraComponent &>(value);
        return Result<Json>::success({{"verticalFovRadians", camera.verticalFovRadians},
                                      {"nearPlane", camera.nearPlane},
                                      {"farPlane", camera.farPlane}});
    };
    codec.create = [](Scene &scene, NodeId node, const Json &data, ComponentLoadContext &) {
        const auto fov = number(data, "verticalFovRadians");
        const auto nearPlane = number(data, "nearPlane");
        const auto farPlane = number(data, "farPlane");
        if (!fov || !nearPlane || !farPlane || fov.value <= 0.0f || fov.value >= 3.14159265f ||
            nearPlane.value <= 0.0f || farPlane.value <= nearPlane.value)
            return Result<ComponentId>::failure("Invalid camera projection fields");
        auto added = scene.addComponent<CameraComponent>(node);
        if (!added)
            return added;
        auto *camera = static_cast<CameraComponent *>(scene.component(added.value));
        camera->verticalFovRadians = fov.value;
        camera->nearPlane = nearPlane.value;
        camera->farPlane = farPlane.value;
        return added;
    };
    return registry.registerCodec(std::move(codec));
}

Result<void> registerBox(ComponentRegistry &registry) {
    ComponentCodec codec;
    codec.type = "ursa.BoxRender";
    codec.matches = [](const Component &value) { return dynamic_cast<const BoxRenderComponent *>(&value) != nullptr; };
    codec.save = [](const Component &value, const ComponentSaveContext &context) -> Result<Json> {
        const auto &box = static_cast<const BoxRenderComponent &>(value);
        Json material = nullptr;
        if (box.material) {
            if (!context.materialId)
                return Result<Json>::failure("No material ID resolver for BoxRenderComponent");
            auto id = context.materialId(box.material);
            if (!id || !id.value)
                return Result<Json>::failure("Cannot resolve BoxRenderComponent material ID: " + id.error);
            material = id.value.toString();
        }
        return Result<Json>::success({{"color", colorJson(box.color)}, {"material", std::move(material)}});
    };
    codec.create = [](Scene &scene, NodeId node, const Json &data, ComponentLoadContext &context) {
        auto selectedColor = color(data, "color");
        if (!selectedColor || !data.is_object() || !data.contains("material"))
            return Result<ComponentId>::failure("Invalid box renderer fields");
        auto added = scene.addComponent<BoxRenderComponent>(node, selectedColor.value);
        if (!added)
            return added;
        auto &material = data["material"];
        if (!material.is_null()) {
            if (!material.is_string())
                return Result<ComponentId>::failure("Invalid box renderer material ID");
            auto id = ResourceId::parse(material.get<String>());
            if (!id)
                return Result<ComponentId>::failure(id.error);
            auto loaded = context.resources().loadMaterial(id.value);
            if (!loaded)
                return Result<ComponentId>::failure(loaded.error);
            static_cast<BoxRenderComponent *>(scene.component(added.value))->material = std::move(loaded.value);
        }
        return added;
    };
    return registry.registerCodec(std::move(codec));
}

Result<void> registerDirectionalLight(ComponentRegistry &registry) {
    ComponentCodec codec;
    codec.type = "ursa.DirectionalLight";
    codec.matches = [](const Component &value) {
        return dynamic_cast<const DirectionalLightComponent *>(&value) != nullptr;
    };
    codec.save = [](const Component &value, const ComponentSaveContext &) -> Result<Json> {
        const auto &light = static_cast<const DirectionalLightComponent &>(value);
        return Result<Json>::success({{"color", colorJson(light.color())}, {"intensity", light.intensity()}});
    };
    codec.create = [](Scene &scene, NodeId node, const Json &data, ComponentLoadContext &) {
        const auto selectedColor = color(data, "color");
        const auto intensity = number(data, "intensity");
        if (!selectedColor || !intensity)
            return Result<ComponentId>::failure(!selectedColor ? selectedColor.error : intensity.error);
        auto added = scene.addComponent<DirectionalLightComponent>(node);
        if (!added)
            return added;
        auto *light = static_cast<DirectionalLightComponent *>(scene.component(added.value));
        auto changed = light->setColor(selectedColor.value);
        if (changed)
            changed = light->setIntensity(intensity.value);
        return changed ? added : Result<ComponentId>::failure(changed.error);
    };
    return registry.registerCodec(std::move(codec));
}

Result<void> registerPointLight(ComponentRegistry &registry) {
    ComponentCodec codec;
    codec.type = "ursa.PointLight";
    codec.matches = [](const Component &value) { return dynamic_cast<const PointLightComponent *>(&value) != nullptr; };
    codec.save = [](const Component &value, const ComponentSaveContext &) -> Result<Json> {
        const auto &light = static_cast<const PointLightComponent &>(value);
        return Result<Json>::success(
            {{"color", colorJson(light.color())}, {"intensity", light.intensity()}, {"range", light.range()}});
    };
    codec.create = [](Scene &scene, NodeId node, const Json &data, ComponentLoadContext &) {
        const auto selectedColor = color(data, "color");
        const auto intensity = number(data, "intensity");
        const auto range = number(data, "range");
        if (!selectedColor || !intensity || !range)
            return Result<ComponentId>::failure(!selectedColor ? selectedColor.error
                                                : !intensity   ? intensity.error
                                                               : range.error);
        auto added = scene.addComponent<PointLightComponent>(node);
        if (!added)
            return added;
        auto *light = static_cast<PointLightComponent *>(scene.component(added.value));
        auto changed = light->setColor(selectedColor.value);
        if (changed)
            changed = light->setIntensity(intensity.value);
        if (changed)
            changed = light->setRange(range.value);
        return changed ? added : Result<ComponentId>::failure(changed.error);
    };
    return registry.registerCodec(std::move(codec));
}
} // namespace

Result<void> registerBuiltinComponents(ComponentRegistry &registry) {
    auto result = registerCamera(registry);
    if (result)
        result = registerBox(registry);
    if (result)
        result = registerDirectionalLight(registry);
    if (result)
        result = registerPointLight(registry);
    return result;
}
} // namespace ursa
