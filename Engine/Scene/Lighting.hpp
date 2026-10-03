#pragma once
#include <Core/Containers.hpp>
#include <Material/Color.hpp>
#include <Math/Vec3.hpp>
#include <Scene/Component.hpp>
#include <cstdint>
#include <vector>

namespace Ursa
{
struct DirectionalLightData
{
	Vec3 direction{0.0f, 0.0f, -1.0f};
	Color color{};
	float intensity = 1.0f;
};

struct PointLightData
{
	Vec3 position{};
	Color color{};
	float intensity = 1.0f;
	float range = 10.0f;
};

struct SceneLighting
{
	static constexpr uint32_t maxDirectionalLights = 4;
	static constexpr uint32_t maxPointLights = 8;

	Color ambientColor{1.0f, 1.0f, 1.0f, 1.0f};
	float ambientIntensity = 0.1f;
	Vector<DirectionalLightData> directional;
	Vector<PointLightData> point;
};

class DirectionalLightComponent final : public Component
{
public:
	Result<void> SetColor(Ursa::Color color);

	Result<void> SetIntensity(float intensity);

	Ursa::Color Color() const
	{
		return color;
	}

	float Intensity() const
	{
		return intensity;
	}

private:
	Ursa::Color color{};
	float intensity = 1.0f;
};

class PointLightComponent final : public Component
{
public:
	Result<void> SetColor(Ursa::Color color);

	Result<void> SetIntensity(float intensity);

	Result<void> SetRange(float range);

	Ursa::Color Color() const
	{
		return color;
	}

	float Intensity() const
	{
		return intensity;
	}

	float Range() const
	{
		return range;
	}

private:
	Ursa::Color color{};
	float intensity = 1.0f;
	float range = 10.0f;
};

bool ValidLightColor(Ursa::Color color);
} // namespace Ursa
