#include <scene/Lighting.hpp>
#include <cmath>

namespace ursa {
bool validLightColor(Color color) {
    return std::isfinite(color.red) && std::isfinite(color.green) && std::isfinite(color.blue) &&
           std::isfinite(color.alpha) && color.red >= 0.0f && color.green >= 0.0f && color.blue >= 0.0f &&
           color.alpha >= 0.0f && color.alpha <= 1.0f;
}

Result<void> DirectionalLightComponent::setColor(Color color) {
    if (!validLightColor(color))
        return Result<void>::failure("Directional light color must be finite and nonnegative");
    color_ = color;
    return {};
}

Result<void> DirectionalLightComponent::setIntensity(float intensity) {
    if (!std::isfinite(intensity) || intensity < 0.0f)
        return Result<void>::failure("Directional light intensity must be finite and nonnegative");
    intensity_ = intensity;
    return {};
}

Result<void> PointLightComponent::setColor(Color color) {
    if (!validLightColor(color))
        return Result<void>::failure("Point light color must be finite and nonnegative");
    color_ = color;
    return {};
}

Result<void> PointLightComponent::setIntensity(float intensity) {
    if (!std::isfinite(intensity) || intensity < 0.0f)
        return Result<void>::failure("Point light intensity must be finite and nonnegative");
    intensity_ = intensity;
    return {};
}

Result<void> PointLightComponent::setRange(float range) {
    if (!std::isfinite(range) || range <= 0.0f)
        return Result<void>::failure("Point light range must be finite and positive");
    range_ = range;
    return {};
}
} // namespace ursa
