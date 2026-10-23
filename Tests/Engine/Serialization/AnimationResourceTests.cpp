#include <Animation/AnimatorComponent.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/Scene.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace Ursa;
using Json = nlohmann::json;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Path(const char* value)
{
	return Take(VirtualPath::Parse(value));
}

void WriteCbor(VirtualFileSystem& files, const VirtualPath& path, const Json& value)
{
	const std::vector<UInt8> encoded = Json::to_cbor(value);
	Vector<UInt8> bytes;
	bytes.reserve(encoded.size());
	for (UInt8 byte : encoded)
	{
		bytes.push_back(byte);
	}
	Done(files.WriteAtomic(path, bytes));
}

bool Near(Float32 left, Float32 right, Float32 epsilon = 0.001f)
{
	return std::abs(left - right) <= epsilon;
}

void Run()
{
	const auto root = std::filesystem::current_path() / "temp" /
	                  ("UrsaAnimationResources-" + ToStdString(ResourceId::Generate().ToString()));
	std::filesystem::create_directories(root);
	try
	{
		VirtualFileSystem files;
		Done(files.Mount(projectScheme, root, MountAccess::ReadWrite));
		const ResourceId modelGuid = ResourceId::Generate();
		const VirtualPath skeletonPath = Path("prj:fixture.skeleton");
		const VirtualPath clipPath = Path("prj:fixture.animation");
		const Json identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
		WriteCbor(
		    files, skeletonPath,
		    {{"format", "UrsaSkeleton"},
		     {"version", 1},
		     {"localFileId", 1},
		     {"joints",
		      {{{"index", 0},
		        {"nodeLocalFileId", 1},
		        {"parentJointIndex", nullptr},
		        {"name", "Root"},
		        {"bindLocalTransform", {{"position", {5, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {2, 2, 2}}}},
		        {"inverseBindMatrix", identity}},
		       {{"index", 1},
		        {"nodeLocalFileId", 2},
		        {"parentJointIndex", 0},
		        {"name", "UnanimatedChild"},
		        {"bindLocalTransform", {{"position", {0, 3, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}},
		        {"inverseBindMatrix", identity}}}}});
		WriteCbor(files, clipPath,
		          {{"format", "UrsaAnimationClip"},
		           {"version", 2},
		           {"localFileId", 2},
		           {"name", "InterpolationFixture"},
		           {"durationSeconds", 2.0},
		           {"channels",
		            {{{"nodeLocalFileId", 1},
		              {"positionInterpolation", "CUBICSPLINE"},
		              {"rotationInterpolation", "LINEAR"},
		              {"scaleInterpolation", "STEP"},
		              {"positionKeys",
		               {{0.0, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
		                {0.5, {1, 0, 0}, {0, 0, 0}, {2, 0, 0}},
		                {2.0, {4, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
		              {"rotationKeys", {{0.0, {0, 0, 0, 1}}, {2.0, {0, 0, 1, 0}}}},
		              {"scaleKeys", {{0.0, {1, 1, 1}}, {2.0, {3, 3, 3}}}}},
		             {{"nodeLocalFileId", 2},
		              {"positionInterpolation", "LINEAR"},
		              {"rotationInterpolation", "LINEAR"},
		              {"scaleInterpolation", "LINEAR"},
		              {"positionKeys", Json::array()},
		              {"rotationKeys", {{0.0, {0, 0, 0, 1}}, {2.0, {0, 0, 0.0001, 1}}}},
		              {"scaleKeys", Json::array()}}}}});

		auto resources = Take(
		    ResourceSession::Open(files,
		                          [&](ResourceId guid, UInt64 localFileId)
		                          {
			                          if (guid != modelGuid)
			                          {
				                          return Result<VirtualPath>::Failure("Unexpected animation test GUID");
			                          }
			                          if (localFileId == 1)
			                          {
				                          return Result<VirtualPath>::Success(skeletonPath);
			                          }
			                          if (localFileId == 2)
			                          {
				                          return Result<VirtualPath>::Success(clipPath);
			                          }
			                          return Result<VirtualPath>::Failure("Unexpected animation test local file ID");
		                          }));
		const auto clip = Take(resources->LoadAnimationClip(modelGuid, 2));
		Expect(clip->channels.size() == 2 &&
		           clip->channels[0].positionInterpolation == AnimationInterpolation::CubicSpline &&
		           Near(clip->channels[0].positions[1].outTangent.x, 2.0f) && clip->channels[1].positions.empty() &&
		           clip->channels[1].scales.empty(),
		       "Animation v2 did not preserve interpolation and cubic tangents");

		Scene scene;
		auto object = Take(scene.CreateGameObject("Animation Fixture"));
		auto animator = Take(object->AddComponent<AnimatorComponent>());
		Done(animator->SetAnimation(modelGuid, 1, 2, *resources));
		animator->SetLoop(false);
		Expect(animator->JointGlobalTransforms().size() == 2, "Animator did not evaluate all skeleton joints");
		Done(animator->SetTime(1.0));
		const Mat4& root = animator->JointGlobalTransforms()[0];
		Expect(Near(root.At(0, 3), 2.222222f),
		       "Cubic spline did not use duration-scaled Hermite tangents on an uneven interval");
		Expect(Near(root.At(0, 0), 0.0f) && Near(root.At(1, 0), 1.0f),
		       "Linear rotation was not spherically sampled at the midpoint");
		const Float32 xAxisScale =
		    std::sqrt(root.At(0, 0) * root.At(0, 0) + root.At(1, 0) * root.At(1, 0) + root.At(2, 0) * root.At(2, 0));
		Expect(Near(xAxisScale, 1.0f), "Step scale did not hold the preceding key");
		const Mat4& child = animator->JointGlobalTransforms()[1];
		Expect(Near(child.At(0, 3), -0.777778f) && Near(child.At(1, 3), 0.0f),
		       "A missing animation channel did not retain its bind transform under the parent pose");
		const Float32 childAxisScale = std::sqrt(child.At(0, 0) * child.At(0, 0) + child.At(1, 0) * child.At(1, 0) +
		                                         child.At(2, 0) * child.At(2, 0));
		Expect(Near(childAxisScale, 1.0f), "Near-identical quaternion keys produced a non-normalized rotation");
		Done(animator->SetTime(2.0));
		Expect(Near(animator->JointGlobalTransforms()[0].At(0, 3), 4.0f),
		       "Sampling the exact end time missed the final key");

		const Vector<UInt8> originalBytes = Take(files.Read(clipPath));
		std::vector<UInt8> standardBytes;
		standardBytes.reserve(originalBytes.size());
		for (UInt8 byte : originalBytes)
		{
			standardBytes.push_back(byte);
		}
		const Json validClip = Json::from_cbor(standardBytes.begin(), standardBytes.end());
		auto expectInvalidClip = [&](Json invalid, const char* message)
		{
			WriteCbor(files, clipPath, invalid);
			auto freshResources = Take(resources->Refreshed());
			const auto malformed = freshResources->LoadAnimationClip(modelGuid, 2);
			Expect(!malformed, message);
		};
		Json invalidTime = validClip;
		invalidTime["channels"][0]["positionKeys"][1][0] = 0.0;
		expectInvalidClip(invalidTime, "Animation resource accepted non-increasing key times");
		Json invalidTangent = validClip;
		invalidTangent["channels"][0]["positionKeys"][1][3][0] = std::numeric_limits<Float32>::infinity();
		expectInvalidClip(invalidTangent, "Animation resource accepted a non-finite cubic tangent");
		Json invalidRotation = validClip;
		invalidRotation["channels"][0]["rotationKeys"][1][1] = {0, 0, 0, 0};
		expectInvalidClip(invalidRotation, "Animation resource accepted a zero quaternion");
	}
	catch (...)
	{
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
		throw;
	}
	std::error_code ignored;
	std::filesystem::remove_all(root, ignored);
}
} // namespace

int main()
{
	try
	{
		Run();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
