#pragma once

#include <Core/Result.hpp>
#include <Core/Types.hpp>
#include <Math/Mat4.hpp>

namespace Ursa::Rendering
{
struct CameraData;

struct DirectionalShadowCascadeSettings
{
	UInt32 cascadeCount = 2;
	Float32 shadowDistance = 50.0f;
	Vector<Float32> splitFractions{0.25f};
	UInt32 atlasWidth = 2048;
	UInt32 atlasHeight = 1024;
};

struct DirectionalShadowCascade
{
	UInt32 index = 0;
	Float32 nearDepth = 0.0f;
	Float32 farDepth = 0.0f;
	Float32 worldUnitsPerTexel = 0.0f;
	UInt32 atlasX = 0;
	UInt32 atlasY = 0;
	UInt32 resolution = 0;
	Mat4 lightView = Mat4::Identity();
	Mat4 lightProjection = Mat4::Identity();
	Mat4 worldToShadow = Mat4::Identity();
};

Result<Vector<DirectionalShadowCascade>>
ComputeDirectionalShadowCascades(const CameraData& camera, Vec3 lightDirection,
                                 const DirectionalShadowCascadeSettings& settings);
} // namespace Ursa::Rendering
