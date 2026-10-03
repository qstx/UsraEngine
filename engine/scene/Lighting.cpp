#include <scene/Lighting.hpp>
#include <cmath>

namespace Ursa
{
bool ValidLightColor(Ursa::Color color)
{
	return std::isfinite(color.red) && std::isfinite(color.green) && std::isfinite(color.blue) &&
	       std::isfinite(color.alpha) && color.red >= 0.0f && color.green >= 0.0f && color.blue >= 0.0f &&
	       color.alpha >= 0.0f && color.alpha <= 1.0f;
}

Result<void> DirectionalLightComponent::SetColor(Ursa::Color color)
{
	if (!ValidLightColor(color))
	{
		return Result<void>::Failure("Directional light color must be finite and nonnegative");
	}
	this->color = color;
	return {};
}

Result<void> DirectionalLightComponent::SetIntensity(float intensity)
{
	if (!std::isfinite(intensity) || intensity < 0.0f)
	{
		return Result<void>::Failure("Directional light intensity must be finite and nonnegative");
	}
	this->intensity = intensity;
	return {};
}

Result<void> PointLightComponent::SetColor(Ursa::Color color)
{
	if (!ValidLightColor(color))
	{
		return Result<void>::Failure("Point light color must be finite and nonnegative");
	}
	this->color = color;
	return {};
}

Result<void> PointLightComponent::SetIntensity(float intensity)
{
	if (!std::isfinite(intensity) || intensity < 0.0f)
	{
		return Result<void>::Failure("Point light intensity must be finite and nonnegative");
	}
	this->intensity = intensity;
	return {};
}

Result<void> PointLightComponent::SetRange(float range)
{
	if (!std::isfinite(range) || range <= 0.0f)
	{
		return Result<void>::Failure("Point light range must be finite and positive");
	}
	this->range = range;
	return {};
}
} // namespace Ursa
