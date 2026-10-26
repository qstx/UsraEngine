using System;
namespace Ursa;

[Ursa.Bindings.WireValue]
public struct SubMesh
{
	public uint FirstIndex;
	public uint IndexCount;
	public int BaseVertex;

	public SubMesh(uint firstIndex, uint indexCount, int baseVertex = 0)
	{
		FirstIndex = firstIndex; IndexCount = indexCount; BaseVertex = baseVertex;
	}
}

[Ursa.Bindings.WireValue]
public struct Bounds
{
	public Vector3 Minimum;
	public Vector3 Maximum;
	public bool Empty;
}

[Ursa.Bindings.WireValue]
public sealed class MeshData
{
	public Vector3[] Positions { get; set; } = Array.Empty<Vector3>();
	public Vector3[] Normals { get; set; } = Array.Empty<Vector3>();
	public Vector2[] Uv0 { get; set; } = Array.Empty<Vector2>();
	public Color[] Colors { get; set; } = Array.Empty<Color>();
	public Vector4[] Tangents { get; set; } = Array.Empty<Vector4>();
	public uint[] Indices { get; set; } = Array.Empty<uint>();
	public SubMesh[] SubMeshes { get; set; } = Array.Empty<SubMesh>();
	public Bounds Bounds { get; set; } = new() { Empty = true };
}

public sealed class Mesh : RenderResource
{
	private Mesh(NativeHandle handle) : base(handle) { }

	internal static Mesh? FromHandle(NativeHandle handle) => Wrap(handle, value => new Mesh(value));

	public static Mesh Create() => FromHandle(NativeMeshBindings.Create())!;

	public static Mesh Load(AssetReference reference) => FromHandle(NativeMeshBindings.Load(reference.Guid, reference.LocalFileId))!;
	public AssetReference? AssetIdentity => NativeResourceBindings.GetMeshAssetIdentity(Handle)?.ToManaged();

	public Mesh Clone() => FromHandle(NativeMeshBindings.Clone(Handle))!;

	public void Destroy() => NativeMeshBindings.Destroy(Handle);

	public ulong Version => NativeMeshBindings.GetVersion(Handle);

	public MeshData GetData() => NativeMeshBindings.GetData(Handle);

	public void SetData(MeshData data) => NativeMeshBindings.SetData(Handle, data ?? throw new ArgumentNullException(nameof(data)));

	public Vector3[] Vertices { get => GetData().Positions; set => NativeMeshBindings.SetVertices(Handle, value); }
	public Vector3[] Normals { get => GetData().Normals; set => NativeMeshBindings.SetNormals(Handle, value); }
	public Vector2[] Uv0 { get => GetData().Uv0; set => NativeMeshBindings.SetUv0(Handle, value); }
	public Color[] Colors { get => GetData().Colors; set => NativeMeshBindings.SetColors(Handle, value); }
	public Vector4[] Tangents { get => GetData().Tangents; set => NativeMeshBindings.SetTangents(Handle, value); }
	public uint[] Indices { get => GetData().Indices; set => NativeMeshBindings.SetIndices(Handle, value); }
	public Bounds Bounds => GetData().Bounds;

	public int SubMeshCount { get => GetData().SubMeshes.Length; set => NativeMeshBindings.SetSubMeshCount(Handle, value); }

	public SubMesh GetSubMesh(int index) => GetData().SubMeshes[index];

	public void SetSubMesh(int index, SubMesh data) => NativeMeshBindings.SetSubMesh(Handle, index, data);

	public uint[] GetTriangles(int subMesh) => NativeMeshBindings.GetTriangles(Handle, subMesh);

	public void SetTriangles(uint[] triangles, int subMesh = 0) => NativeMeshBindings.SetTriangles(Handle, subMesh, triangles);

	public void Clear() => NativeMeshBindings.Clear(Handle);

	public void RecalculateNormals() => NativeMeshBindings.RecalculateNormals(Handle);

	public void RecalculateBounds() => NativeMeshBindings.RecalculateBounds(Handle);
}
