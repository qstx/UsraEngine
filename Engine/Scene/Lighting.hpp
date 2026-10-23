#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Material/Color.hpp>
#include <Math/Vec3.hpp>
#include <Serialization/ResourceId.hpp>
#include <Scene/Component.hpp>
#include <cstdint>

namespace Ursa
{
struct DirectionalLightData
{
	Vec3 direction{0.0f, 0.0f, -1.0f};
	Color color{};
	Ursa::Float32 intensity = 1.0f;
	bool castsShadows = true;
	Ursa::Float32 shadowStrength = 1.0f;
	Ursa::Float32 shadowBias = 0.001f;
	Ursa::Float32 shadowNormalBias = 0.01f;
	bool softShadows = false;
	ResourceId identity;
	Ursa::UInt32 layer = 0;
};

struct PointLightData
{
	Vec3 position{};
	Color color{};
	Ursa::Float32 intensity = 1.0f;
	Ursa::Float32 range = 10.0f;
	ResourceId identity;
	Ursa::UInt32 layer = 0;
};

struct SceneLighting
{
	static constexpr Ursa::UInt32 maxDirectionalLights = 4;
	static constexpr Ursa::UInt32 maxPointLights = 8;

	Color ambientColor{1.0f, 1.0f, 1.0f, 1.0f};
	Ursa::Float32 ambientIntensity = 0.1f;
	Vector<DirectionalLightData> directional;
	Vector<PointLightData> point;
};

class DirectionalLightComponent final : public Component
{
public:
	String SerializationType() const override
	{
		return "ursa.DirectionalLight";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	Result<void> SetColor(Ursa::Color lightColor);

	Result<void> SetIntensity(Ursa::Float32 lightIntensity);

	Result<void> SetCastsShadows(bool value)
	{
		castsShadows = value;
		return {};
	}

	Result<void> SetShadowStrength(Ursa::Float32 value);

	Result<void> SetShadowBias(Ursa::Float32 value);

	Result<void> SetShadowNormalBias(Ursa::Float32 value);

	Result<void> SetSoftShadows(bool value)
	{
		softShadows = value;
		return {};
	}

	bool CastsShadows() const
	{
		return castsShadows;
	}

	Ursa::Float32 ShadowStrength() const
	{
		return shadowStrength;
	}

	Ursa::Float32 ShadowBias() const
	{
		return shadowBias;
	}

	Ursa::Float32 ShadowNormalBias() const
	{
		return shadowNormalBias;
	}

	bool SoftShadows() const
	{
		return softShadows;
	}

	Ursa::Color Color() const
	{
		return color;
	}

	Ursa::Float32 Intensity() const
	{
		return intensity;
	}

private:
	Ursa::Color color{};
	Ursa::Float32 intensity = 1.0f;
	bool castsShadows = true;
	Ursa::Float32 shadowStrength = 1.0f;
	Ursa::Float32 shadowBias = 0.001f;
	Ursa::Float32 shadowNormalBias = 0.01f;
	bool softShadows = false;
};

class PointLightComponent final : public Component
{
public:
	String SerializationType() const override
	{
		return "ursa.PointLight";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	Result<void> SetColor(Ursa::Color lightColor);

	Result<void> SetIntensity(Ursa::Float32 lightIntensity);

	Result<void> SetRange(Ursa::Float32 lightRange);

	Ursa::Color Color() const
	{
		return color;
	}

	Ursa::Float32 Intensity() const
	{
		return intensity;
	}

	Ursa::Float32 Range() const
	{
		return range;
	}

private:
	Ursa::Color color{};
	Ursa::Float32 intensity = 1.0f;
	Ursa::Float32 range = 10.0f;
};

bool ValidLightColor(Ursa::Color color);
bool ValidDirectionalLightShadowSettings(Ursa::Float32 strength, Ursa::Float32 bias, Ursa::Float32 normalBias);
} // namespace Ursa
