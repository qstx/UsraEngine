#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Material/Color.hpp>
#include <Math/Vec3.hpp>
#include <Scene/Component.hpp>
#include <cstdint>

namespace Ursa
{
struct DirectionalLightData
{
	Vec3 direction{0.0f, 0.0f, -1.0f};
	Color color{};
	Ursa::Float32 intensity = 1.0f;
};

struct PointLightData
{
	Vec3 position{};
	Color color{};
	Ursa::Float32 intensity = 1.0f;
	Ursa::Float32 range = 10.0f;
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
} // namespace Ursa
