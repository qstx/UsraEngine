#pragma once

#include <Animation/AnimationAssets.hpp>
#include <Scene/Component.hpp>

namespace Ursa
{
class ResourceSession;

class AnimatorComponent final : public Component
{
public:
	String SerializationType() const override
	{
		return "ursa.Animator";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	Result<void> SetAnimation(ResourceId modelGuid, Ursa::UInt64 skeletonLocalFileId, Ursa::UInt64 clipLocalFileId,
	                          ResourceSession& resources);

	Result<void> Play();

	void Stop()
	{
		playing = false;
	}

	Result<void> SetTime(Ursa::Float64 timeSeconds);

	Result<void> SetSpeed(Ursa::Float32 value);

	void SetLoop(bool value)
	{
		loop = value;
	}

	bool IsPlaying() const
	{
		return playing;
	}

	Ursa::Float64 Time() const
	{
		return timeSeconds;
	}

	Ursa::Float32 Speed() const
	{
		return speed;
	}

	bool Loop() const
	{
		return loop;
	}

	ResourceId ModelGuid() const
	{
		return modelGuid;
	}

	Ursa::UInt64 SkeletonLocalFileId() const
	{
		return skeletonLocalFileId;
	}

	Ursa::UInt64 ClipLocalFileId() const
	{
		return clipLocalFileId;
	}

	const Vector<Mat4>& JointPalette() const
	{
		return jointPalette;
	}

	Result<void> OnUpdate(const FrameContext& context) override;

private:
	Result<void> EvaluatePose();

	ResourceId modelGuid;
	Ursa::UInt64 skeletonLocalFileId = 0;
	Ursa::UInt64 clipLocalFileId = 0;
	std::shared_ptr<const ModelSkeleton> skeleton;
	std::shared_ptr<const AnimationClip> clip;
	Vector<Mat4> jointPalette;
	Ursa::Float64 timeSeconds = 0.0;
	Ursa::Float32 speed = 1.0f;
	bool loop = true;
	bool playing = false;
};
} // namespace Ursa
