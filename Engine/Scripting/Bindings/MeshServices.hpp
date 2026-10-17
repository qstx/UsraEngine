#pragma once

#include <BindingTypes.generated.hpp>
#include <Mesh/Mesh.hpp>

namespace Ursa::Scripting::Bindings
{
struct MeshServices
{
	using Handle = Wire::Value_Ursa_NativeHandle;
	using Data = Wire::Value_Ursa_MeshData;
	using Section = Wire::Value_Ursa_SubMesh;
	static MeshPtr Resolve(BindingCall& call, const Handle& mesh);
	static Result<Handle> Create(BindingCall& call);
	static Result<Handle> Load(BindingCall& call, const Utf8View& guid, UInt64 localFileId);
	static Result<Handle> Clone(BindingCall& call, const Handle& mesh);
	static Result<void> Destroy(BindingCall& call, const Handle& mesh);
	static Result<UInt64> GetVersion(BindingCall& call, const Handle& mesh);
	static Result<Data> GetData(BindingCall& call, const Handle& mesh);
	static Result<void> SetData(BindingCall& call, const Handle& mesh, const Data& data);
	static Result<void> SetVertices(BindingCall& call, const Handle& mesh, const DataView& values);
	static Result<void> SetNormals(BindingCall& call, const Handle& mesh, const DataView& values);
	static Result<void> SetUv0(BindingCall& call, const Handle& mesh, const DataView& values);
	static Result<void> SetColors(BindingCall& call, const Handle& mesh, const DataView& values);
	static Result<void> SetTangents(BindingCall& call, const Handle& mesh, const DataView& values);
	static Result<void> SetIndices(BindingCall& call, const Handle& mesh, const DataView& values);
	static Result<void> SetSubMeshCount(BindingCall& call, const Handle& mesh, Int32 count);
	static Result<void> SetSubMesh(BindingCall& call, const Handle& mesh, Int32 index, const Section& value);
	static Result<DataView> GetTriangles(BindingCall& call, const Handle& mesh, Int32 index);
	static Result<void> SetTriangles(BindingCall& call, const Handle& mesh, Int32 index, const DataView& values);
	static Result<void> Clear(BindingCall& call, const Handle& mesh);
	static Result<void> RecalculateNormals(BindingCall& call, const Handle& mesh);
	static Result<void> RecalculateBounds(BindingCall& call, const Handle& mesh);
};
} // namespace Ursa::Scripting::Bindings
