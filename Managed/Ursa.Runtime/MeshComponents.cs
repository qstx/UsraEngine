using System;
using System.Linq;
using System.Text.Json;
namespace Ursa;

[NativeComponentType("ursa.MeshFilter")]
public sealed class MeshFilterComponent : Component
{
	public Mesh? SharedMesh
	{
		get => Mesh.FromHandle(EngineApi.ResourceRequest<NativeHandle>(new { operation = "binding.mesh.get", handle = NativeHandle }));
		set => EngineApi.ResourceRequest(new { operation = "binding.mesh.set", handle = NativeHandle, data = value?.Handle });
	}

	public Mesh Mesh
	{
		get => Mesh.FromHandle(EngineApi.ResourceRequest<NativeHandle>(new { operation = "binding.mesh.get", handle = NativeHandle, instance = true }))!;
		set => EngineApi.ResourceRequest(new { operation = "binding.mesh.set", handle = NativeHandle, instance = true, data = value?.Handle });
	}
}

public abstract class RendererComponent : Component
{
	private Material?[] Get(bool instance, bool first = false)
	{
		var values = EngineApi.ResourceRequest(new { operation = "binding.materials.get", handle = NativeHandle, instance, first });
		return values.EnumerateArray().Select(value => value.ValueKind == System.Text.Json.JsonValueKind.Null ? null :
			Ursa.Material.FromHandle(value.Deserialize<NativeHandle>(EngineApi.ResourceJson))).ToArray();
	}

	private void Set(Material?[] values, bool shared, bool first = false) => EngineApi.ResourceRequest(new
	{
		operation = "binding.materials.set", handle = NativeHandle, shared, first, data = values.Select(value => value?.Handle).ToArray()
	});

	public Material? SharedMaterial { get => Get(false, true)[0]; set => Set(new[] { value }, true, true); }
	public Material?[] SharedMaterials { get => Get(false); set => Set(value, true); }
	public Material? Material { get => Get(true, true)[0]; set => Set(new[] { value }, false, true); }
	public Material?[] Materials { get => Get(true); set => Set(value, false); }
}

[NativeComponentType("ursa.MeshRenderer")]
public sealed class MeshRendererComponent : RendererComponent { }

[NativeComponentType("ursa.SkinnedMeshRenderer")]
public sealed class SkinnedMeshRendererComponent : RendererComponent
{
	public Mesh? SharedMesh
	{
		get => Mesh.FromHandle(EngineApi.ResourceRequest<NativeHandle>(new { operation = "binding.mesh.get", handle = NativeHandle }));
		set => EngineApi.ResourceRequest(new { operation = "binding.mesh.set", handle = NativeHandle, data = value?.Handle });
	}
}
