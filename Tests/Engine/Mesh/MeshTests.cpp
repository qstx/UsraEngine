#include <Mesh/Mesh.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Core/StringInterop.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace Ursa;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

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

MeshData Triangle()
{
	MeshData data;
	data.positions = {{0, 0, 0}, {2, 0, 0}, {0, 3, 0}};
	data.indices = {0, 1, 2};
	return data;
}

void DataAndTransactions()
{
	auto mesh = Take(Mesh::Create(Triangle()));
	const auto before = mesh->Snapshot();
	auto invalid = Triangle();
	invalid.indices[2] = 3;
	Expect(!mesh->SetData(invalid), "Out-of-range index was accepted");
	Expect(mesh->Snapshot() == before, "Failed write replaced the snapshot");
	invalid = Triangle();
	invalid.positions[0].x = std::numeric_limits<Float32>::infinity();
	Expect(!mesh->SetData(invalid), "Infinite position was accepted");
	Expect(!mesh->SetNormals({{1, 0, 0}}), "Mismatched normals were accepted");
	Expect(!mesh->SetIndices({0, 1}), "Incomplete triangle was accepted");
	Expect(!mesh->SetSubMeshCount(Mesh::maxSubMeshes + 1), "Too many submeshes accepted");
	invalid = Triangle();
	invalid.subMeshes = {{UINT32_MAX, 3, 0}};
	Expect(!mesh->SetData(invalid), "Overflowing submesh accepted");
	auto copied = Take(mesh->GetData());
	copied.positions[0].x = 99;
	Expect(mesh->Snapshot()->data.positions[0].x == 0, "Getter exposed mutable source memory");
	auto clone = Take(mesh->Clone());
	Expect(clone->Identity() != mesh->Identity(), "Clone reused runtime identity");
	Done(clone->SetVertices({{1, 0, 0}, {2, 0, 0}, {0, 3, 0}}));
	Expect(mesh->Snapshot()->data.positions[0].x == 0 && before->data.positions[0].x == 0,
	       "Clone or subsequent write modified an existing snapshot");
	Done(mesh->RecalculateNormals());
	Expect(std::abs(mesh->Snapshot()->data.normals[0].z - 1) < 0.0001f, "Triangle normal is incorrect");
	Expect(mesh->Snapshot()->data.bounds.maximum.y == 3, "Mesh bounds were not calculated");
	Done(mesh->SetSubMeshCount(2));
	Done(mesh->SetTriangles(1, {2, 1, 0}));
	Expect(Take(mesh->GetTriangles(0))[2] == 2 && Take(mesh->GetTriangles(1))[0] == 2,
	       "Submesh editing destroyed another submesh");
	Expect(!mesh->SetSubMesh(0, {0, 3, -1}), "Invalid base vertex accepted");
	Done(mesh->Clear());
	Expect(mesh->Snapshot()->data.positions.empty() && mesh->Snapshot()->data.bounds.empty, "Clear retained geometry");
}

void SerializationAndLifetime()
{
	auto mesh = Take(Mesh::Create(Triangle()));
	SerializationContext context;
	ArchiveWriter writer;
	Done(mesh->Serialize(writer));
	for (bool binary : {false, true})
	{
		auto bytes = Take(binary ? BinaryArchive::Encode(writer) : JsonArchive::Encode(writer));
		auto reader = Take(binary ? BinaryArchive::Decode(bytes) : JsonArchive::Decode(bytes));
		auto restored = Take(Mesh::Create());
		Done(restored->Deserialize(reader, context));
		Expect(restored->Snapshot()->data.indices == mesh->Snapshot()->data.indices &&
		           restored->Snapshot()->data.positions[2].y == 3,
		       "Mesh archive lost geometry");
		Expect(restored->Identity() != mesh->Identity(), "Archive restored a process identity");
	}
	auto noDraw = Take(mesh->Clone());
	Done(noDraw->SetSubMeshCount(0));
	ArchiveWriter noDrawWriter;
	Done(noDraw->Serialize(noDrawWriter));
	auto restoredNoDraw = Take(Mesh::Create());
	Done(restoredNoDraw->Deserialize(Take(JsonArchive::Decode(Take(JsonArchive::Encode(noDrawWriter)))), context));
	Expect(restoredNoDraw->Snapshot()->data.subMeshes.empty(), "Archive invented a submesh for a no-draw mesh");
	auto offsetData = Triangle();
	offsetData.indices = {3, 4, 5};
	offsetData.subMeshes = {{0, 3, -3}};
	auto offsetMesh = Take(Mesh::Create(offsetData));
	Expect(Take(offsetMesh->GetTriangles(0))[0] == 0, "Base vertex was not applied to triangle indices");
	auto retained = mesh->Snapshot();
	Done(mesh->Destroy());
	Expect(!mesh->IsValid() && !mesh->GetData() && !mesh->Clone() && !mesh->SetData(Triangle()),
	       "Destroyed Mesh remained mutable");
	Expect(retained->data.positions.size() == 3, "Retained submitted snapshot was invalidated");
	auto asset = Take(Mesh::Create());
	Done(asset->BindAssetIdentity({ResourceId::Generate(), 0}));
	Expect(!asset->Destroy() && asset->IsValid(), "Persistent asset was destroyed");
	auto copy = Take(asset->Clone());
	Expect(!copy->AssetIdentity().guid, "Clone inherited external asset identity");
	Done(copy->Destroy());
}

void SkinCloneAndValidation()
{
	auto skeleton = std::make_shared<ModelSkeleton>();
	skeleton->modelGuid = ResourceId::Generate();
	skeleton->localFileId = 7;
	skeleton->joints.resize(1);
	auto data = Triangle();
	data.skeletonReference = {skeleton->modelGuid, skeleton->localFileId};
	data.skeleton = skeleton;
	data.skinInfluences.resize(3);
	for (auto& influence : data.skinInfluences)
	{
		influence.weights[0] = 1;
	}
	auto mesh = Take(Mesh::Create(data));
	auto clone = Take(mesh->Clone());
	Expect(clone->Snapshot()->data.skeleton == skeleton && clone->Snapshot()->data.skinInfluences.size() == 3,
	       "Clone dropped the skin binding");
	const auto before = clone->Snapshot();
	data.skinInfluences[0].weights[0] = 2;
	Expect(!clone->SetData(data) && clone->Snapshot() == before, "Invalid skin weights changed the mesh");
	data.skinInfluences[0].weights[0] = 1;
	data.skinInfluences[0].jointIndices[0] = 1;
	Expect(!clone->SetData(data), "Out-of-range joint was accepted");
	ArchiveWriter writer;
	Done(mesh->Serialize(writer));
	auto reader = Take(BinaryArchive::Decode(Take(BinaryArchive::Encode(writer))));
	SerializationContext withoutSkeletons;
	Expect(!clone->Deserialize(reader, withoutSkeletons) && clone->Snapshot() == before,
	       "Missing external skeleton replaced valid mesh state");
}
} // namespace

int main()
{
	try
	{
		DataAndTransactions();
		SerializationAndLifetime();
		SkinCloneAndValidation();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
