using System;
namespace Ursa;

public struct Vector2
{
	public float X;
	public float Y;

	public Vector2(float x, float y) { X = x; Y = y; }
}

public struct Vector4
{
	public float X;
	public float Y;
	public float Z;
	public float W;

	public Vector4(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }
}

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

public struct Bounds
{
	public Vector3 Minimum;
	public Vector3 Maximum;
	public bool Empty;
}

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

	public static Mesh Create() => FromHandle(EngineApi.ResourceRequest<NativeHandle>(new { operation = "mesh.create" }))!;

	public static Mesh Load(AssetReference reference) => FromHandle(EngineApi.ResourceRequest<NativeHandle>(new
	{
		operation = "mesh.load", guid = reference.Guid, localFileId = reference.LocalFileId
	}))!;

	public Mesh Clone() => FromHandle(EngineApi.ResourceRequest<NativeHandle>(new { operation = "mesh.clone", handle = Handle }))!;

	public void Destroy() => EngineApi.ResourceRequest(new { operation = "mesh.destroy", handle = Handle });

	public ulong Version => EngineApi.ResourceRequest<ulong>(new { operation = "mesh.version", handle = Handle });

	public MeshData GetData() => EngineApi.ResourceRequest<MeshData>(new { operation = "mesh.get", handle = Handle });

	public void SetData(MeshData data) => Set("mesh.set", data ?? throw new ArgumentNullException(nameof(data)));

	public Vector3[] Vertices { get => GetData().Positions; set => Set("mesh.vertices", value); }
	public Vector3[] Normals { get => GetData().Normals; set => Set("mesh.normals", value); }
	public Vector2[] Uv0 { get => GetData().Uv0; set => Set("mesh.uv0", value); }
	public Color[] Colors { get => GetData().Colors; set => Set("mesh.colors", value); }
	public Vector4[] Tangents { get => GetData().Tangents; set => Set("mesh.tangents", value); }
	public uint[] Indices { get => GetData().Indices; set => Set("mesh.indices", value); }
	public Bounds Bounds => GetData().Bounds;

	public int SubMeshCount { get => GetData().SubMeshes.Length; set => Set("mesh.subMeshCount", value); }

	public SubMesh GetSubMesh(int index) => GetData().SubMeshes[index];

	public void SetSubMesh(int index, SubMesh data) => EngineApi.ResourceRequest(new
	{
		operation = "mesh.subMesh", handle = Handle, index, data
	});

	public uint[] GetTriangles(int subMesh) => EngineApi.ResourceRequest<uint[]>(new
	{
		operation = "mesh.triangles.get", handle = Handle, index = subMesh
	});

	public void SetTriangles(uint[] triangles, int subMesh = 0) => EngineApi.ResourceRequest(new
	{
		operation = "mesh.triangles.set", handle = Handle, index = subMesh, data = triangles
	});

	public void Clear() => Set("mesh.clear", null);

	public void RecalculateNormals() => Set("mesh.recalculateNormals", null);

	public void RecalculateBounds() => Set("mesh.recalculateBounds", null);

	private void Set(string operation, object? data) => EngineApi.ResourceRequest(new { operation, handle = Handle, data });
}
