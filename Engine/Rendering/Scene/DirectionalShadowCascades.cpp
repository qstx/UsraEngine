#include <Rendering/Scene/DirectionalShadowCascades.hpp>
#include <Rendering/Scene/CameraData.hpp>
#include <algorithm>
#include <cmath>

namespace Ursa::Rendering
{
namespace
{
constexpr Float32 epsilon = 0.00001f;

Vec3 Add(Vec3 left, Vec3 right)
{
	return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 Subtract(Vec3 left, Vec3 right)
{
	return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 Multiply(Vec3 value, Float32 scalar)
{
	return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Result<Vec3> TransformPoint(const Mat4& matrix, Vec3 point)
{
	const Float32 x =
	    matrix.At(0, 0) * point.x + matrix.At(0, 1) * point.y + matrix.At(0, 2) * point.z + matrix.At(0, 3);
	const Float32 y =
	    matrix.At(1, 0) * point.x + matrix.At(1, 1) * point.y + matrix.At(1, 2) * point.z + matrix.At(1, 3);
	const Float32 z =
	    matrix.At(2, 0) * point.x + matrix.At(2, 1) * point.y + matrix.At(2, 2) * point.z + matrix.At(2, 3);
	const Float32 w =
	    matrix.At(3, 0) * point.x + matrix.At(3, 1) * point.y + matrix.At(3, 2) * point.z + matrix.At(3, 3);
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w) || std::abs(w) <= epsilon)
	{
		return Result<Vec3>::Failure("Camera projection produced an invalid frustum corner");
	}
	return Result<Vec3>::Success({x / w, y / w, z / w});
}

Result<Vector<Vec3>> FrustumSliceCorners(const CameraData& camera, Float32 nearDepth, Float32 farDepth)
{
	Mat4 inverseProjection;
	Mat4 inverseView;
	try
	{
		inverseProjection = camera.projection.Inverse();
		inverseView = camera.view.Inverse();
	}
	catch (const std::exception& error)
	{
		return Result<Vector<Vec3>>::Failure(String("Camera matrices cannot be inverted for shadow cascades: ") +
		                                     error.what());
	}

	Vector<Vec3> corners;
	corners.reserve(8);
	for (Float32 y : {-1.0f, 1.0f})
	{
		for (Float32 x : {-1.0f, 1.0f})
		{
			const Float32 clipDepth = camera.orthographic ? 0.0f : 1.0f;
			const auto projected = TransformPoint(inverseProjection, {x, y, clipDepth});
			if (!projected)
			{
				return Result<Vector<Vec3>>::Failure(projected.error);
			}
			const Vec3 viewPoint = projected.value;
			if (camera.orthographic)
			{
				for (Float32 depth : {nearDepth, farDepth})
				{
					const auto worldPoint = TransformPoint(inverseView, {viewPoint.x, viewPoint.y, -depth});
					if (!worldPoint)
					{
						return Result<Vector<Vec3>>::Failure(worldPoint.error);
					}
					corners.push_back(worldPoint.value);
				}
			}
			else
			{
				if (viewPoint.z >= -epsilon)
				{
					return Result<Vector<Vec3>>::Failure("Perspective projection has an invalid shadow frustum ray");
				}
				for (Float32 depth : {nearDepth, farDepth})
				{
					const Float32 scale = depth / -viewPoint.z;
					const auto worldPoint =
					    TransformPoint(inverseView, {viewPoint.x * scale, viewPoint.y * scale, -depth});
					if (!worldPoint)
					{
						return Result<Vector<Vec3>>::Failure(worldPoint.error);
					}
					corners.push_back(worldPoint.value);
				}
			}
		}
	}
	return Result<Vector<Vec3>>::Success(std::move(corners));
}

Mat4 AtlasTransform(UInt32 x, UInt32 y, UInt32 width, UInt32 height, UInt32 atlasWidth, UInt32 atlasHeight)
{
	Mat4 result = Mat4::Identity();
	const Float32 scaleX = Float32(width) / Float32(atlasWidth) * 0.5f;
	const Float32 scaleY = Float32(height) / Float32(atlasHeight) * 0.5f;
	result.At(0, 0) = scaleX;
	result.At(0, 3) = Float32(x) / Float32(atlasWidth) + scaleX;
	result.At(1, 1) = scaleY;
	result.At(1, 3) = Float32(y) / Float32(atlasHeight) + scaleY;
	return result;
}
} // namespace

Result<Vector<DirectionalShadowCascade>>
ComputeDirectionalShadowCascades(const CameraData& camera, Vec3 lightDirection,
                                 const DirectionalShadowCascadeSettings& settings)
{
	if (settings.cascadeCount != 1 && settings.cascadeCount != 2 && settings.cascadeCount != 4)
	{
		return Result<Vector<DirectionalShadowCascade>>::Failure("Directional shadow cascade count must be 1, 2, or 4");
	}
	if (!std::isfinite(settings.shadowDistance) || settings.shadowDistance <= camera.nearPlane ||
	    !std::isfinite(camera.nearPlane) || !std::isfinite(camera.farPlane) || camera.farPlane <= camera.nearPlane)
	{
		return Result<Vector<DirectionalShadowCascade>>::Failure(
		    "Directional shadow distance or camera clipping planes are invalid");
	}
	if (settings.splitFractions.size() != settings.cascadeCount - 1)
	{
		return Result<Vector<DirectionalShadowCascade>>::Failure(
		    "Directional shadow split count does not match the cascade count");
	}
	Float32 previousSplit = 0.0f;
	for (Float32 split : settings.splitFractions)
	{
		if (!std::isfinite(split) || split <= previousSplit || split >= 1.0f)
		{
			return Result<Vector<DirectionalShadowCascade>>::Failure(
			    "Directional shadow split fractions must strictly increase inside (0, 1)");
		}
		previousSplit = split;
	}
	if (!settings.atlasWidth || !settings.atlasHeight)
	{
		return Result<Vector<DirectionalShadowCascade>>::Failure("Directional shadow atlas dimensions must be nonzero");
	}
	if (UInt64(settings.atlasWidth) * settings.atlasHeight > 64ull * 1024 * 1024)
	{
		return Result<Vector<DirectionalShadowCascade>>::Failure(
		    "Directional shadow atlas exceeds the 256 MiB render-target budget");
	}
	const UInt32 columns = settings.cascadeCount == 2 ? 2u : settings.cascadeCount == 4 ? 2u : 1u;
	const UInt32 rows = settings.cascadeCount == 4 ? 2u : 1u;
	if (settings.atlasWidth % columns || settings.atlasHeight % rows ||
	    settings.atlasWidth / columns != settings.atlasHeight / rows)
	{
		return Result<Vector<DirectionalShadowCascade>>::Failure(
		    "Directional shadow atlas must divide into equal square cascade tiles");
	}
	const UInt32 tileResolution = settings.atlasWidth / columns;
	const Float32 lightLengthSquared = Dot(lightDirection, lightDirection);
	if (!std::isfinite(lightLengthSquared) || lightLengthSquared <= epsilon * epsilon)
	{
		return Result<Vector<DirectionalShadowCascade>>::Failure(
		    "Directional shadow light direction must be finite and nonzero");
	}
	lightDirection = Multiply(lightDirection, 1.0f / std::sqrt(lightLengthSquared));
	const Vec3 upReference =
	    std::abs(Dot(lightDirection, {0.0f, 1.0f, 0.0f})) > 0.98f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
	const Vec3 lightRight = Normalized(Cross(lightDirection, upReference));
	const Vec3 lightUp = Normalized(Cross(lightRight, lightDirection));

	const Float32 effectiveFar = std::min(camera.farPlane, settings.shadowDistance);
	const Float32 range = effectiveFar - camera.nearPlane;
	Vector<DirectionalShadowCascade> result;
	result.reserve(settings.cascadeCount);
	Float32 cascadeNear = camera.nearPlane;
	for (UInt32 index = 0; index < settings.cascadeCount; ++index)
	{
		const Float32 cascadeFar = index + 1 == settings.cascadeCount
		                               ? effectiveFar
		                               : camera.nearPlane + range * settings.splitFractions[index];
		const auto cornerResult = FrustumSliceCorners(camera, cascadeNear, cascadeFar);
		if (!cornerResult)
		{
			return Result<Vector<DirectionalShadowCascade>>::Failure(cornerResult.error);
		}
		const Vector<Vec3>& corners = cornerResult.value;
		Vec3 center{};
		for (const Vec3 corner : corners)
		{
			center = Add(center, corner);
		}
		center = Multiply(center, 1.0f / Float32(corners.size()));
		Float32 radius = 0.0f;
		for (const Vec3 corner : corners)
		{
			const Vec3 delta = Subtract(corner, center);
			radius = std::max(radius, std::sqrt(Dot(delta, delta)));
		}
		if (!std::isfinite(radius) || radius <= epsilon)
		{
			return Result<Vector<DirectionalShadowCascade>>::Failure(
			    "Directional shadow cascade has an invalid frustum radius");
		}
		const Float32 worldUnitsPerTexel = (2.0f * radius) / Float32(tileResolution);
		const Float32 centerRight = Dot(center, lightRight);
		const Float32 centerUp = Dot(center, lightUp);
		const Float32 snappedRight = std::round(centerRight / worldUnitsPerTexel) * worldUnitsPerTexel;
		const Float32 snappedUp = std::round(centerUp / worldUnitsPerTexel) * worldUnitsPerTexel;
		center =
		    Add(center, Add(Multiply(lightRight, snappedRight - centerRight), Multiply(lightUp, snappedUp - centerUp)));

		const Float32 lightDepth = radius * 2.0f + 0.1f;
		const Mat4 lightView = Mat4::LookAt(Subtract(center, Multiply(lightDirection, lightDepth)), center, lightUp);
		const Mat4 lightProjection = Mat4::Orthographic(radius * 2.0f, 1.0f, 0.1f, radius * 4.0f + 0.2f);
		const UInt32 tileX = (index % columns) * tileResolution;
		const UInt32 tileY = (index / columns) * tileResolution;
		DirectionalShadowCascade cascade;
		cascade.index = index;
		cascade.nearDepth = cascadeNear;
		cascade.farDepth = cascadeFar;
		cascade.worldUnitsPerTexel = worldUnitsPerTexel;
		cascade.atlasX = tileX;
		cascade.atlasY = tileY;
		cascade.resolution = tileResolution;
		cascade.lightView = lightView;
		cascade.lightProjection = lightProjection;
		cascade.worldToShadow =
		    AtlasTransform(tileX, tileY, tileResolution, tileResolution, settings.atlasWidth, settings.atlasHeight) *
		    lightProjection * lightView;
		result.push_back(std::move(cascade));
		cascadeNear = cascadeFar;
	}
	return Result<Vector<DirectionalShadowCascade>>::Success(std::move(result));
}
} // namespace Ursa::Rendering
