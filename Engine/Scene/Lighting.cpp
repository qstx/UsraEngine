#include <Core/Types.hpp>
#include <Scene/Lighting.hpp>
#include <cmath>

namespace Ursa
{
bool ValidLightColor(Ursa::Color color)
{
	return std::isfinite(color.red) && std::isfinite(color.green) && std::isfinite(color.blue) &&
	       std::isfinite(color.alpha) && color.red >= 0.0f && color.green >= 0.0f && color.blue >= 0.0f &&
	       color.alpha >= 0.0f && color.alpha <= 1.0f;
}

Result<void> DirectionalLightComponent::SetColor(Ursa::Color lightColor)
{
	if (!ValidLightColor(lightColor))
	{
		return Result<void>::Failure("Directional light color must be finite and nonnegative");
	}
	this->color = lightColor;
	return {};
}

Result<void> DirectionalLightComponent::SetIntensity(Ursa::Float32 lightIntensity)
{
	if (!std::isfinite(lightIntensity) || lightIntensity < 0.0f)
	{
		return Result<void>::Failure("Directional light intensity must be finite and nonnegative");
	}
	this->intensity = lightIntensity;
	return {};
}

Result<void> PointLightComponent::SetColor(Ursa::Color lightColor)
{
	if (!ValidLightColor(lightColor))
	{
		return Result<void>::Failure("Point light color must be finite and nonnegative");
	}
	this->color = lightColor;
	return {};
}

Result<void> PointLightComponent::SetIntensity(Ursa::Float32 lightIntensity)
{
	if (!std::isfinite(lightIntensity) || lightIntensity < 0.0f)
	{
		return Result<void>::Failure("Point light intensity must be finite and nonnegative");
	}
	this->intensity = lightIntensity;
	return {};
}

Result<void> PointLightComponent::SetRange(Ursa::Float32 lightRange)
{
	if (!std::isfinite(lightRange) || lightRange <= 0.0f)
	{
		return Result<void>::Failure("Point light range must be finite and positive");
	}
	this->range = lightRange;
	return {};
}
} // namespace Ursa
