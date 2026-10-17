#include <Scripting/Bindings/MeshServices.hpp>
#include <Scripting/Bindings/RuntimeServices.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/Bindings/BindingMath.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Serialization/ResourceSession.hpp>

namespace Ursa::Scripting::Bindings
{
namespace
{
auto Vector3Values(const DataView& values)
{
	return ReadArray<Wire::Value_Ursa_Vector3, Vec3>(
	    values, [](const auto& v) { return ReadVector3(v); }, Mesh::maxDataBytes);
}

auto Vector2Values(const DataView& values)
{
	return ReadArray<Wire::Value_Ursa_Vector2, Vec2>(
	    values, [](const auto& v) { return Vec2{v.F_X, v.F_Y}; }, Mesh::maxDataBytes);
}

auto Vector4Values(const DataView& values)
{
	return ReadArray<Wire::Value_Ursa_Vector4, Vec4>(
	    values, [](const auto& v) { return Vec4{v.F_X, v.F_Y, v.F_Z, v.F_W}; }, Mesh::maxDataBytes);
}

auto ColorValues(const DataView& values)
{
	return ReadArray<Wire::Value_Ursa_Color, Color>(
	    values, [](const auto& v) { return ReadColor(v); }, Mesh::maxDataBytes);
}

auto Indices(const DataView& values)
{
	return ReadArray<UInt32, UInt32>(values, [](UInt32 v) { return v; }, Mesh::maxDataBytes);
}

SubMesh SectionValue(const Wire::Value_Ursa_SubMesh& value)
{
	return {value.F_FirstIndex, value.F_IndexCount, value.F_BaseVertex};
}
} // namespace

MeshPtr MeshServices::Resolve(BindingCall& call, const Handle& mesh)
{
	return TakeBindingResult(RuntimeServices::SceneApi(call).ResolveMesh(ReadManagedHandle(mesh)));
}

Result<MeshServices::Handle> MeshServices::Create(BindingCall& call)
{
	auto& api = RuntimeServices::SceneApi(call);
	return Result<Handle>::Success(
	    WriteManagedHandle<Handle>(TakeBindingResult(api.RegisterMesh(TakeBindingResult(Mesh::Create())))));
}

Result<MeshServices::Handle> MeshServices::Load(BindingCall& call, const Utf8View& guid, UInt64 localFileId)
{
	auto& api = RuntimeServices::SceneApi(call);
	if (!api.resources)
	{
		return Result<Handle>::Failure("Mesh loading requires a ResourceSession");
	}
	const auto id = TakeBindingResult(ResourceId::Parse(ReadString(guid)));
	return Result<Handle>::Success(WriteManagedHandle<Handle>(
	    TakeBindingResult(api.RegisterMesh(TakeBindingResult(api.resources->LoadMesh(id, localFileId))))));
}

Result<MeshServices::Handle> MeshServices::Clone(BindingCall& call, const Handle& mesh)
{
	auto& api = RuntimeServices::SceneApi(call);
	return Result<Handle>::Success(WriteManagedHandle<Handle>(
	    TakeBindingResult(api.RegisterMesh(TakeBindingResult(Resolve(call, mesh)->Clone())))));
}

Result<void> MeshServices::Destroy(BindingCall& call, const Handle& mesh)
{
	auto& api = RuntimeServices::SceneApi(call);
	CompleteBindingResult(Resolve(call, mesh)->Destroy());
	CompleteBindingResult(api.handles->Invalidate(ReadManagedHandle(mesh)));
	api.ownedMeshes.erase(mesh.F_index);
	return {};
}

Result<UInt64> MeshServices::GetVersion(BindingCall& call, const Handle& mesh)
{
	return Result<UInt64>::Success(Resolve(call, mesh)->Version());
}

Result<MeshServices::Data> MeshServices::GetData(BindingCall& call, const Handle& mesh)
{
	const auto source = Resolve(call, mesh)->Snapshot();
	if (!source)
	{
		return Result<Data>::Failure("Mesh snapshot is unavailable");
	}
	const auto& data = source->data;
	Data value;
	value.present = 1;
	value.F_Positions = WriteArray<Wire::Value_Ursa_Vector3>(call.arena, data.positions, [](Vec3 v)
	                                                         { return WriteVector3<Wire::Value_Ursa_Vector3>(v); });
	value.F_Normals = WriteArray<Wire::Value_Ursa_Vector3>(call.arena, data.normals, [](Vec3 v)
	                                                       { return WriteVector3<Wire::Value_Ursa_Vector3>(v); });
	value.F_Uv0 = WriteArray<Wire::Value_Ursa_Vector2>(call.arena, data.uv0,
	                                                   [](Vec2 v) { return Wire::Value_Ursa_Vector2{v.x, v.y}; });
	value.F_Colors = WriteArray<Wire::Value_Ursa_Color>(call.arena, data.colors,
	                                                    [](Color v) { return WriteColor<Wire::Value_Ursa_Color>(v); });
	value.F_Tangents = WriteArray<Wire::Value_Ursa_Vector4>(call.arena, data.tangents, [](Vec4 v)
	                                                        { return Wire::Value_Ursa_Vector4{v.x, v.y, v.z, v.w}; });
	value.F_Indices = call.arena.Array(data.indices.data(), data.indices.size());
	value.F_SubMeshes = WriteArray<Section>(call.arena, data.subMeshes, [](SubMesh v)
	                                        { return Section{v.firstIndex, v.indexCount, v.baseVertex}; });
	value.F_Bounds.F_Minimum = WriteVector3<Wire::Value_Ursa_Vector3>(data.bounds.minimum);
	value.F_Bounds.F_Maximum = WriteVector3<Wire::Value_Ursa_Vector3>(data.bounds.maximum);
	value.F_Bounds.F_Empty = data.bounds.empty ? 1u : 0u;
	return Result<Data>::Success(value);
}

Result<void> MeshServices::SetData(BindingCall& call, const Handle& mesh, const Data& data)
{
	auto target = Resolve(call, mesh);
	MeshData value;
	const auto previous = target->Snapshot();
	if (previous)
	{
		value.skeletonReference = previous->data.skeletonReference;
		value.skeleton = previous->data.skeleton;
		value.skinInfluences = previous->data.skinInfluences;
	}
	value.positions = Vector3Values(data.F_Positions);
	value.normals = Vector3Values(data.F_Normals);
	value.uv0 = Vector2Values(data.F_Uv0);
	value.colors = ColorValues(data.F_Colors);
	value.tangents = Vector4Values(data.F_Tangents);
	value.indices = Indices(data.F_Indices);
	value.subMeshes = ReadArray<Section, SubMesh>(
	    data.F_SubMeshes, [](const auto& v) { return SectionValue(v); }, Mesh::maxDataBytes);
	value.bounds = {ReadVector3(data.F_Bounds.F_Minimum), ReadVector3(data.F_Bounds.F_Maximum),
	                data.F_Bounds.F_Empty != 0};
	return target->SetData(std::move(value));
}

Result<void> MeshServices::SetVertices(BindingCall& call, const Handle& mesh, const DataView& values)
{
	return Resolve(call, mesh)->SetVertices(Vector3Values(values));
}

Result<void> MeshServices::SetNormals(BindingCall& call, const Handle& mesh, const DataView& values)
{
	return Resolve(call, mesh)->SetNormals(Vector3Values(values));
}

Result<void> MeshServices::SetUv0(BindingCall& call, const Handle& mesh, const DataView& values)
{
	return Resolve(call, mesh)->SetUv0(Vector2Values(values));
}

Result<void> MeshServices::SetColors(BindingCall& call, const Handle& mesh, const DataView& values)
{
	return Resolve(call, mesh)->SetColors(ColorValues(values));
}

Result<void> MeshServices::SetTangents(BindingCall& call, const Handle& mesh, const DataView& values)
{
	return Resolve(call, mesh)->SetTangents(Vector4Values(values));
}

Result<void> MeshServices::SetIndices(BindingCall& call, const Handle& mesh, const DataView& values)
{
	return Resolve(call, mesh)->SetIndices(Indices(values));
}

Result<void> MeshServices::SetSubMeshCount(BindingCall& call, const Handle& mesh, Int32 count)
{
	if (count < 0)
	{
		return Result<void>::Failure("SubMesh count cannot be negative");
	}
	return Resolve(call, mesh)->SetSubMeshCount(Size(count));
}

Result<void> MeshServices::SetSubMesh(BindingCall& call, const Handle& mesh, Int32 index, const Section& value)
{
	if (index < 0)
	{
		return Result<void>::Failure("SubMesh index cannot be negative");
	}
	return Resolve(call, mesh)->SetSubMesh(Size(index), SectionValue(value));
}

Result<DataView> MeshServices::GetTriangles(BindingCall& call, const Handle& mesh, Int32 index)
{
	if (index < 0)
	{
		return Result<DataView>::Failure("SubMesh index cannot be negative");
	}
	const auto values = TakeBindingResult(Resolve(call, mesh)->GetTriangles(Size(index)));
	return Result<DataView>::Success(call.arena.Array(values.data(), values.size()));
}

Result<void> MeshServices::SetTriangles(BindingCall& call, const Handle& mesh, Int32 index, const DataView& values)
{
	if (index < 0)
	{
		return Result<void>::Failure("SubMesh index cannot be negative");
	}
	return Resolve(call, mesh)->SetTriangles(Size(index), Indices(values));
}

Result<void> MeshServices::Clear(BindingCall& call, const Handle& mesh)
{
	return Resolve(call, mesh)->Clear();
}

Result<void> MeshServices::RecalculateNormals(BindingCall& call, const Handle& mesh)
{
	return Resolve(call, mesh)->RecalculateNormals();
}

Result<void> MeshServices::RecalculateBounds(BindingCall& call, const Handle& mesh)
{
	return Resolve(call, mesh)->RecalculateBounds();
}
} // namespace Ursa::Scripting::Bindings
