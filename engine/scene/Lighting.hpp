#pragma once
#include <core/Containers.hpp>
#include <material/Color.hpp>
#include <math/Vec3.hpp>
#include <scene/Component.hpp>
#include <cstdint>
#include <vector>

namespace ursa {
struct DirectionalLightData {
    Vec3 direction{0.0f, 0.0f, -1.0f};
    Color color{};
    float intensity = 1.0f;
};

struct PointLightData {
    Vec3 position{};
    Color color{};
    float intensity = 1.0f;
    float range = 10.0f;
};

struct SceneLighting {
    static constexpr uint32_t maxDirectionalLights = 4;
    static constexpr uint32_t maxPointLights = 8;

    Color ambientColor{1.0f, 1.0f, 1.0f, 1.0f};
    float ambientIntensity = 0.1f;
    Vector<DirectionalLightData> directional;
    Vector<PointLightData> point;
};

class DirectionalLightComponent final : public Component {
public:
    Result<void> setColor(Color color);

    Result<void> setIntensity(float intensity);

    Color color() const {
        return color_;
    }

    float intensity() const {
        return intensity_;
    }

private:
    Color color_{};
    float intensity_ = 1.0f;
};

class PointLightComponent final : public Component {
public:
    Result<void> setColor(Color color);

    Result<void> setIntensity(float intensity);

    Result<void> setRange(float range);

    Color color() const {
        return color_;
    }

    float intensity() const {
        return intensity_;
    }

    float range() const {
        return range_;
    }

private:
    Color color_{};
    float intensity_ = 1.0f;
    float range_ = 10.0f;
};

bool validLightColor(Color color);
} // namespace ursa
