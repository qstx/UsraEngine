using System;
using System.Linq;
namespace Ursa;

[NativeComponentType("ursa.MeshFilter")]
public sealed class MeshFilterComponent : Component
{
	public Mesh? SharedMesh
	{
		get => Mesh.FromHandle(NativeResourceBindings.GetMeshBinding(NativeHandle, false) ?? default);
		set => NativeResourceBindings.SetMeshBinding(NativeHandle, value?.Handle, false);
	}

	public Mesh Mesh
	{
		get => Mesh.FromHandle(NativeResourceBindings.GetMeshBinding(NativeHandle, true) ?? default)!;
		set => NativeResourceBindings.SetMeshBinding(NativeHandle, value?.Handle, true);
	}
}

public abstract class RendererComponent : Component
{
	private Material?[] Get(bool instance, bool first = false)
	{
		return NativeResourceBindings.GetMaterialBindings(NativeHandle, instance, first)
			.Select(value => value.HasValue ? Ursa.Material.FromHandle(value.Value) : null).ToArray();
	}

	private void Set(Material?[] values, bool shared, bool first = false) => NativeResourceBindings.SetMaterialBindings(
		NativeHandle, values.Select(value => value?.Handle).ToArray(), shared, first);

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
		get => Mesh.FromHandle(NativeResourceBindings.GetMeshBinding(NativeHandle, false) ?? default);
		set => NativeResourceBindings.SetMeshBinding(NativeHandle, value?.Handle, false);
	}
}
