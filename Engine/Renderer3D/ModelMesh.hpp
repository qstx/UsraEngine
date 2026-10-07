#pragma once

#include <Animation/AnimationAssets.hpp>
#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Serialization/ResourceId.hpp>
#include <memory>

namespace Ursa
{
class MaterialInstance;

struct ModelSkinInfluence
{
	Array<Ursa::UInt16, 4> jointIndices{};
	Array<Ursa::Float32, 4> weights{};
};

struct ModelMesh
{
	ResourceId modelGuid;
	Ursa::UInt64 localFileId = 0;
	Ursa::UInt32 vertexStride = 0;
	Vector<Ursa::UInt8> vertexBytes;
	Vector<Ursa::UInt32> indices;
	std::shared_ptr<MaterialInstance> material;
	Ursa::UInt64 skeletonLocalFileId = 0;
	std::shared_ptr<const ModelSkeleton> skeleton;
	Vector<ModelSkinInfluence> skinInfluences;
};

using ModelMeshPtr = std::shared_ptr<const ModelMesh>;
} // namespace Ursa
