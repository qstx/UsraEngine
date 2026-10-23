#include <Animation/AnimatorComponent.hpp>

#include <Serialization/ArchiveFields.hpp>
#include <Serialization/AssetReference.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/SerializationContext.hpp>
#include <algorithm>
#include <cmath>

namespace Ursa
{
namespace
{
Vec3 SampleVector(const Vector<AnimationVectorKey>& keys, AnimationInterpolation interpolation,
                  Ursa::Float64 timeSeconds, Vec3 fallback)
{
	if (keys.empty())
	{
		return fallback;
	}
	if (timeSeconds <= keys.front().timeSeconds)
	{
		return keys.front().value;
	}
	if (timeSeconds >= keys.back().timeSeconds)
	{
		return keys.back().value;
	}
	for (Ursa::Size index = 1; index < keys.size(); ++index)
	{
		if (timeSeconds <= keys[index].timeSeconds)
		{
			const AnimationVectorKey& first = keys[index - 1];
			const AnimationVectorKey& second = keys[index];
			const Ursa::Float64 span = second.timeSeconds - first.timeSeconds;
			const Ursa::Float32 alpha =
			    span > 0.0 ? static_cast<Ursa::Float32>((timeSeconds - first.timeSeconds) / span) : 0.0f;
			if (interpolation == AnimationInterpolation::Step)
			{
				return first.value;
			}
			if (interpolation == AnimationInterpolation::CubicSpline)
			{
				const Ursa::Float32 alpha2 = alpha * alpha;
				const Ursa::Float32 alpha3 = alpha2 * alpha;
				const Ursa::Float32 h00 = 2 * alpha3 - 3 * alpha2 + 1;
				const Ursa::Float32 h10 = alpha3 - 2 * alpha2 + alpha;
				const Ursa::Float32 h01 = -2 * alpha3 + 3 * alpha2;
				const Ursa::Float32 h11 = alpha3 - alpha2;
				return {h00 * first.value.x + h10 * static_cast<Ursa::Float32>(span) * first.outTangent.x +
				            h01 * second.value.x + h11 * static_cast<Ursa::Float32>(span) * second.inTangent.x,
				        h00 * first.value.y + h10 * static_cast<Ursa::Float32>(span) * first.outTangent.y +
				            h01 * second.value.y + h11 * static_cast<Ursa::Float32>(span) * second.inTangent.y,
				        h00 * first.value.z + h10 * static_cast<Ursa::Float32>(span) * first.outTangent.z +
				            h01 * second.value.z + h11 * static_cast<Ursa::Float32>(span) * second.inTangent.z};
			}
			return {first.value.x + (second.value.x - first.value.x) * alpha,
			        first.value.y + (second.value.y - first.value.y) * alpha,
			        first.value.z + (second.value.z - first.value.z) * alpha};
		}
	}
	return keys.back().value;
}

Quat Normalize(Quat value)
{
	const Ursa::Float32 magnitude =
	    std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
	if (!std::isfinite(magnitude) || magnitude <= 0.000001f)
	{
		return {};
	}
	const Ursa::Float32 inverse = 1.0f / magnitude;
	return {value.x * inverse, value.y * inverse, value.z * inverse, value.w * inverse};
}

Quat SampleRotation(const Vector<AnimationRotationKey>& keys, AnimationInterpolation interpolation,
                    Ursa::Float64 timeSeconds, Quat fallback)
{
	if (keys.empty())
	{
		return fallback;
	}
	if (timeSeconds <= keys.front().timeSeconds)
	{
		return Normalize(keys.front().value);
	}
	if (timeSeconds >= keys.back().timeSeconds)
	{
		return Normalize(keys.back().value);
	}
	for (Ursa::Size index = 1; index < keys.size(); ++index)
	{
		if (timeSeconds <= keys[index].timeSeconds)
		{
			const AnimationRotationKey& first = keys[index - 1];
			const AnimationRotationKey& second = keys[index];
			const Ursa::Float64 span = second.timeSeconds - first.timeSeconds;
			const Ursa::Float32 alpha =
			    span > 0.0 ? static_cast<Ursa::Float32>((timeSeconds - first.timeSeconds) / span) : 0.0f;
			if (interpolation == AnimationInterpolation::Step)
			{
				return Normalize(first.value);
			}
			if (interpolation == AnimationInterpolation::CubicSpline)
			{
				const Ursa::Float32 alpha2 = alpha * alpha;
				const Ursa::Float32 alpha3 = alpha2 * alpha;
				const Ursa::Float32 h00 = 2 * alpha3 - 3 * alpha2 + 1;
				const Ursa::Float32 h10 = alpha3 - 2 * alpha2 + alpha;
				const Ursa::Float32 h01 = -2 * alpha3 + 3 * alpha2;
				const Ursa::Float32 h11 = alpha3 - alpha2;
				return Normalize(
				    {h00 * first.value.x + h10 * static_cast<Ursa::Float32>(span) * first.outTangent.x +
				         h01 * second.value.x + h11 * static_cast<Ursa::Float32>(span) * second.inTangent.x,
				     h00 * first.value.y + h10 * static_cast<Ursa::Float32>(span) * first.outTangent.y +
				         h01 * second.value.y + h11 * static_cast<Ursa::Float32>(span) * second.inTangent.y,
				     h00 * first.value.z + h10 * static_cast<Ursa::Float32>(span) * first.outTangent.z +
				         h01 * second.value.z + h11 * static_cast<Ursa::Float32>(span) * second.inTangent.z,
				     h00 * first.value.w + h10 * static_cast<Ursa::Float32>(span) * first.outTangent.w +
				         h01 * second.value.w + h11 * static_cast<Ursa::Float32>(span) * second.inTangent.w});
			}
			Quat end = second.value;
			Ursa::Float32 dot =
			    first.value.x * end.x + first.value.y * end.y + first.value.z * end.z + first.value.w * end.w;
			if (dot < 0.0f)
			{
				end = {-end.x, -end.y, -end.z, -end.w};
				dot = -dot;
			}
			if (interpolation == AnimationInterpolation::Linear && dot < 0.9995f)
			{
				const Ursa::Float32 theta = std::acos((std::max)(-1.0f, (std::min)(1.0f, dot)));
				const Ursa::Float32 sine = std::sin(theta);
				if (std::abs(sine) > 0.000001f)
				{
					const Ursa::Float32 firstWeight = std::sin((1.0f - alpha) * theta) / sine;
					const Ursa::Float32 secondWeight = std::sin(alpha * theta) / sine;
					return Normalize({first.value.x * firstWeight + end.x * secondWeight,
					                  first.value.y * firstWeight + end.y * secondWeight,
					                  first.value.z * firstWeight + end.z * secondWeight,
					                  first.value.w * firstWeight + end.w * secondWeight});
				}
			}
			return Normalize(
			    {first.value.x + (end.x - first.value.x) * alpha, first.value.y + (end.y - first.value.y) * alpha,
			     first.value.z + (end.z - first.value.z) * alpha, first.value.w + (end.w - first.value.w) * alpha});
		}
	}
	return Normalize(keys.back().value);
}
} // namespace

Result<void> AnimatorComponent::Serialize(ArchiveWriter& writer) const
{
	if (!modelGuid || skeletonLocalFileId == 0 || clipLocalFileId == 0 || !skeleton || !clip)
	{
		return Result<void>::Failure("Animator has no imported skeleton and animation clip assigned");
	}
	auto result = WriteAssetReference(writer, "skeleton", {modelGuid, skeletonLocalFileId});
	if (result)
	{
		result = WriteAssetReference(writer, "clip", {modelGuid, clipLocalFileId});
	}
	if (result)
	{
		result = writer.Write("timeSeconds", timeSeconds);
	}
	if (result)
	{
		result = writer.Write("speed", static_cast<Ursa::Float64>(speed));
	}
	if (result)
	{
		result = writer.Write("loop", loop);
	}
	if (result)
	{
		result = writer.Write("playing", playing);
	}
	return result;
}

Result<void> AnimatorComponent::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	ResourceSession* resources = context.ResourceLoader();
	if (!resources)
	{
		return Result<void>::Failure("Animator loading requires a ResourceSession");
	}
	auto skeletonReference = ReadAssetReference(reader, "skeleton");
	auto clipReference = ReadAssetReference(reader, "clip");
	auto loadedTime = reader.Number("timeSeconds");
	auto loadedSpeed = reader.Number("speed");
	auto loadedLoop = reader.Boolean("loop");
	auto loadedPlaying = reader.Boolean("playing");
	if (!skeletonReference || !clipReference || skeletonReference.value.localFileId == 0 ||
	    clipReference.value.localFileId == 0 || skeletonReference.value.guid != clipReference.value.guid ||
	    !loadedTime || !loadedSpeed || !loadedLoop || !loadedPlaying || !std::isfinite(loadedTime.value) ||
	    loadedTime.value < 0.0 || !std::isfinite(loadedSpeed.value) || std::abs(loadedSpeed.value) > 1000.0)
	{
		return Result<void>::Failure("Animator archive contains invalid asset references or playback state");
	}
	auto loadedSkeleton = resources->LoadSkeleton(skeletonReference.value.guid, skeletonReference.value.localFileId);
	if (!loadedSkeleton)
	{
		return Result<void>::Failure(loadedSkeleton.error);
	}
	auto loadedClip = resources->LoadAnimationClip(clipReference.value.guid, clipReference.value.localFileId);
	if (!loadedClip)
	{
		return Result<void>::Failure(loadedClip.error);
	}
	modelGuid = skeletonReference.value.guid;
	skeletonLocalFileId = skeletonReference.value.localFileId;
	clipLocalFileId = clipReference.value.localFileId;
	skeleton = std::move(loadedSkeleton.value);
	clip = std::move(loadedClip.value);
	timeSeconds = clip->durationSeconds > 0.0 && loadedLoop.value ? std::fmod(loadedTime.value, clip->durationSeconds)
	                                                              : (std::min)(loadedTime.value, clip->durationSeconds);
	speed = static_cast<Ursa::Float32>(loadedSpeed.value);
	loop = loadedLoop.value;
	playing = loadedPlaying.value;
	return EvaluatePose();
}

Result<void> AnimatorComponent::SetAnimation(ResourceId selectedModelGuid, Ursa::UInt64 selectedSkeletonLocalFileId,
                                             Ursa::UInt64 selectedClipLocalFileId, ResourceSession& resources)
{
	if (!selectedModelGuid || selectedSkeletonLocalFileId == 0 || selectedClipLocalFileId == 0)
	{
		return Result<void>::Failure("Animator needs valid skeleton and animation clip references");
	}
	auto loadedSkeleton = resources.LoadSkeleton(selectedModelGuid, selectedSkeletonLocalFileId);
	if (!loadedSkeleton)
	{
		return Result<void>::Failure(loadedSkeleton.error);
	}
	auto loadedClip = resources.LoadAnimationClip(selectedModelGuid, selectedClipLocalFileId);
	if (!loadedClip)
	{
		return Result<void>::Failure(loadedClip.error);
	}
	modelGuid = selectedModelGuid;
	skeletonLocalFileId = selectedSkeletonLocalFileId;
	clipLocalFileId = selectedClipLocalFileId;
	skeleton = std::move(loadedSkeleton.value);
	clip = std::move(loadedClip.value);
	timeSeconds = 0.0;
	playing = false;
	return EvaluatePose();
}

Result<void> AnimatorComponent::Play()
{
	if (!skeleton || !clip)
	{
		return Result<void>::Failure("Animator cannot play before a skeleton and clip are assigned");
	}
	playing = true;
	return EvaluatePose();
}

Result<void> AnimatorComponent::SetTime(Ursa::Float64 value)
{
	if (!std::isfinite(value) || value < 0.0)
	{
		return Result<void>::Failure("Animator time must be finite and nonnegative");
	}
	if (clip && clip->durationSeconds > 0.0)
	{
		timeSeconds = loop ? std::fmod(value, clip->durationSeconds) : (std::min)(value, clip->durationSeconds);
	}
	else
	{
		timeSeconds = 0.0;
	}
	return EvaluatePose();
}

Result<void> AnimatorComponent::SetSpeed(Ursa::Float32 value)
{
	if (!std::isfinite(value) || std::abs(value) > 1000.0f)
	{
		return Result<void>::Failure("Animator speed must be finite and within [-1000, 1000]");
	}
	speed = value;
	return {};
}

Result<void> AnimatorComponent::Update(const FrameContext& context)
{
	if (!playing)
	{
		return {};
	}
	if (!std::isfinite(context.deltaSeconds) || context.deltaSeconds < 0.0f)
	{
		return Result<void>::Failure("Animator received an invalid frame delta");
	}
	if (!clip || clip->durationSeconds <= 0.0)
	{
		timeSeconds = 0.0;
		playing = false;
		return EvaluatePose();
	}
	timeSeconds += static_cast<Ursa::Float64>(context.deltaSeconds) * speed;
	if (loop)
	{
		timeSeconds = std::fmod(timeSeconds, clip->durationSeconds);
		if (timeSeconds < 0.0)
		{
			timeSeconds += clip->durationSeconds;
		}
	}
	else if (timeSeconds >= clip->durationSeconds)
	{
		timeSeconds = clip->durationSeconds;
		playing = false;
	}
	else if (timeSeconds <= 0.0 && speed < 0.0f)
	{
		timeSeconds = 0.0;
		playing = false;
	}
	return EvaluatePose();
}

Result<void> AnimatorComponent::EvaluatePose()
{
	if (!skeleton || !clip)
	{
		jointPalette.clear();
		return {};
	}
	Map<Ursa::UInt64, const AnimationNodeChannel*> channelsByNode;
	for (const AnimationNodeChannel& channel : clip->channels)
	{
		channelsByNode.emplace(channel.nodeLocalFileId, &channel);
	}
	Vector<Mat4> globalTransforms;
	globalTransforms.reserve(skeleton->joints.size());
	jointPalette.resize(skeleton->joints.size());
	for (Ursa::Size jointIndex = 0; jointIndex < skeleton->joints.size(); ++jointIndex)
	{
		const SkeletonJoint& joint = skeleton->joints[jointIndex];
		LocalTransform local = joint.bindLocalTransform;
		const auto channel = channelsByNode.find(joint.nodeLocalFileId);
		if (channel != channelsByNode.end())
		{
			local.position = SampleVector(channel->second->positions, channel->second->positionInterpolation,
			                              timeSeconds, local.position);
			local.rotation = SampleRotation(channel->second->rotations, channel->second->rotationInterpolation,
			                                timeSeconds, local.rotation);
			local.scale =
			    SampleVector(channel->second->scales, channel->second->scaleInterpolation, timeSeconds, local.scale);
		}
		const Mat4 global = joint.parentJointIndex < 0
		                        ? local.Matrix()
		                        : globalTransforms[static_cast<Ursa::Size>(joint.parentJointIndex)] * local.Matrix();
		globalTransforms.push_back(global);
		jointPalette[jointIndex] = global * joint.inverseBindMatrix;
	}
	jointGlobalTransforms = std::move(globalTransforms);
	return {};
}
} // namespace Ursa
