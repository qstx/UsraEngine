#pragma once

#include <Animation/AnimationAssets.hpp>
#include <Material/Color.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec4.hpp>
#include <Mesh/Bounds.hpp>
#include <Mesh/SubMesh.hpp>
#include <Serialization/AssetReference.hpp>
#include <memory>

namespace Ursa
{
struct MeshSkinInfluence
{
	Array<UInt16, 4> jointIndices{};
	Array<Float32, 4> weights{};
};

struct MeshData
{
	Vector<Vec3> positions;
	Vector<Vec3> normals;
	Vector<Vec2> uv0;
	Vector<Color> colors;
	Vector<Vec4> tangents;
	Vector<UInt32> indices;
	Vector<SubMesh> subMeshes;
	Bounds bounds;
	AssetReference skeletonReference;
	std::shared_ptr<const ModelSkeleton> skeleton;
	Vector<MeshSkinInfluence> skinInfluences;
	// Skin-local joint slot -> shared node-pose joint index. Empty retains the legacy identity mapping.
	Vector<UInt16> skinJointIndices;
	Vector<Mat4> skinInverseBindMatrices;
};

struct MeshSnapshot
{
	UInt64 version = 0;
	MeshData data;
};
} // namespace Ursa
