#pragma once

#include <Core/Containers.hpp>
#include <Scene/LocalTransform.hpp>
#include <Serialization/ResourceId.hpp>

namespace Ursa
{
enum class AnimationInterpolation : Ursa::UInt8
{
	Linear,
	Step,
	CubicSpline
};

struct SkeletonJoint
{
	Ursa::UInt64 nodeLocalFileId = 0;
	Ursa::Int32 parentJointIndex = -1;
	String name;
	LocalTransform bindLocalTransform;
	Mat4 inverseBindMatrix;
};

struct ModelSkeleton
{
	ResourceId modelGuid;
	Ursa::UInt64 localFileId = 0;
	Vector<SkeletonJoint> joints;
};

struct AnimationVectorKey
{
	Ursa::Float64 timeSeconds = 0.0;
	Vec3 value{};
	Vec3 inTangent{};
	Vec3 outTangent{};
};

struct AnimationRotationKey
{
	Ursa::Float64 timeSeconds = 0.0;
	Quat value{};
	Quat inTangent{0, 0, 0, 0};
	Quat outTangent{0, 0, 0, 0};
};

struct AnimationNodeChannel
{
	Ursa::UInt64 nodeLocalFileId = 0;
	AnimationInterpolation positionInterpolation = AnimationInterpolation::Linear;
	AnimationInterpolation rotationInterpolation = AnimationInterpolation::Linear;
	AnimationInterpolation scaleInterpolation = AnimationInterpolation::Linear;
	Vector<AnimationVectorKey> positions;
	Vector<AnimationRotationKey> rotations;
	Vector<AnimationVectorKey> scales;
};

struct AnimationClip
{
	ResourceId modelGuid;
	Ursa::UInt64 localFileId = 0;
	String name;
	Ursa::Float64 durationSeconds = 0.0;
	Vector<AnimationNodeChannel> channels;
};
} // namespace Ursa
