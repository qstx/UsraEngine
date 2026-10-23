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

bool ValidDirectionalLightShadowSettings(Ursa::Float32 strength, Ursa::Float32 bias, Ursa::Float32 normalBias)
{
	return std::isfinite(strength) && strength >= 0.0f && strength <= 1.0f && std::isfinite(bias) && bias >= 0.0f &&
	       std::isfinite(normalBias) && normalBias >= 0.0f;
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

Result<void> DirectionalLightComponent::SetShadowStrength(Ursa::Float32 value)
{
	if (!ValidDirectionalLightShadowSettings(value, shadowBias, shadowNormalBias))
	{
		return Result<void>::Failure("Directional light shadow strength must be finite and within [0, 1]");
	}
	shadowStrength = value;
	return {};
}

Result<void> DirectionalLightComponent::SetShadowBias(Ursa::Float32 value)
{
	if (!ValidDirectionalLightShadowSettings(shadowStrength, value, shadowNormalBias))
	{
		return Result<void>::Failure("Directional light shadow bias must be finite and nonnegative");
	}
	shadowBias = value;
	return {};
}

Result<void> DirectionalLightComponent::SetShadowNormalBias(Ursa::Float32 value)
{
	if (!ValidDirectionalLightShadowSettings(shadowStrength, shadowBias, value))
	{
		return Result<void>::Failure("Directional light shadow normal bias must be finite and nonnegative");
	}
	shadowNormalBias = value;
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
