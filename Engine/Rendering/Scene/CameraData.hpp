#pragma once

#include <Core/Result.hpp>
#include <Material/Color.hpp>
#include <Math/Mat4.hpp>
#include <Serialization/ResourceId.hpp>

namespace Ursa::Rendering
{
enum class CameraType : UInt32
{
	Game,
	SceneView,
	Preview
};

enum class CameraClearFlags : UInt32
{
	ColorAndDepth,
	Depth,
	Nothing
};

struct Viewport
{
	Float32 x = 0;
	Float32 y = 0;
	Float32 width = 1;
	Float32 height = 1;
};

struct CameraSettings
{
	CameraType type = CameraType::Game;
	Float32 depth = 0;
	CameraClearFlags clearFlags = CameraClearFlags::ColorAndDepth;
	Color background{0.05f, 0.05f, 0.05f, 1};
	UInt32 cullingMask = 0xffffffffu;
	Viewport viewport;
	UInt32 targetWidth = 0;
	UInt32 targetHeight = 0;

	Result<void> Validate() const;
};

struct CameraData
{
	ResourceId identity;
	Mat4 viewProjection = Mat4::Identity();
	Vec3 position;
	CameraSettings settings;
	String debugName;
	Mat4 view = Mat4::Identity();
	Mat4 projection = Mat4::Identity();
	Mat4 inverseViewProjection = Mat4::Identity();
	Float32 nearPlane = 0.1f;
	Float32 farPlane = 100.0f;
	bool orthographic = false;
};
} // namespace Ursa::Rendering
