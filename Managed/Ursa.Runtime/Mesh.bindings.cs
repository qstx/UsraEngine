using Ursa.Bindings;

namespace Ursa;

[BindingModule("Runtime")]
[NativeHeader("Scripting/Bindings/MeshServices.hpp")]
internal static partial class NativeMeshBindings
{
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::Create")]
	internal static partial NativeHandle Create();
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::Load")]
	internal static partial NativeHandle Load(string guid, ulong localFileId);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::Clone")]
	internal static partial NativeHandle Clone([HandleKind(4)] NativeHandle mesh);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::Destroy")]
	internal static partial void Destroy([HandleKind(4)] NativeHandle mesh);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::GetVersion")]
	internal static partial ulong GetVersion([HandleKind(4)] NativeHandle mesh);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::GetData")]
	internal static partial MeshData GetData([HandleKind(4)] NativeHandle mesh);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::SetData")]
	internal static partial void SetData([HandleKind(4)] NativeHandle mesh, MeshData data);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::SetVertices")]
	internal static partial void SetVertices([HandleKind(4)] NativeHandle mesh, Vector3[] values);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::SetNormals")]
	internal static partial void SetNormals([HandleKind(4)] NativeHandle mesh, Vector3[] values);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::SetUv0")]
	internal static partial void SetUv0([HandleKind(4)] NativeHandle mesh, Vector2[] values);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::SetColors")]
	internal static partial void SetColors([HandleKind(4)] NativeHandle mesh, Color[] values);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::SetTangents")]
	internal static partial void SetTangents([HandleKind(4)] NativeHandle mesh, Vector4[] values);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::SetIndices")]
	internal static partial void SetIndices([HandleKind(4)] NativeHandle mesh, uint[] values);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::SetSubMeshCount")]
	internal static partial void SetSubMeshCount([HandleKind(4)] NativeHandle mesh, int count);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::SetSubMesh")]
	internal static partial void SetSubMesh([HandleKind(4)] NativeHandle mesh, int index, SubMesh value);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::GetTriangles")]
	internal static partial uint[] GetTriangles([HandleKind(4)] NativeHandle mesh, int index);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::SetTriangles")]
	internal static partial void SetTriangles([HandleKind(4)] NativeHandle mesh, int index, uint[] values);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::Clear")]
	internal static partial void Clear([HandleKind(4)] NativeHandle mesh);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::RecalculateNormals")]
	internal static partial void RecalculateNormals([HandleKind(4)] NativeHandle mesh);
	[NativeMethod("Ursa::Scripting::Bindings::MeshServices::RecalculateBounds")]
	internal static partial void RecalculateBounds([HandleKind(4)] NativeHandle mesh);
}
