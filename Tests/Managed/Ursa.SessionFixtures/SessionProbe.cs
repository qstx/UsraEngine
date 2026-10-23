using System;
using Ursa;

namespace Ursa.SessionFixtures;

public sealed class SessionProbe : Behaviour
{
	private static int counter;
	public int value = Ursa.SessionDependency.ProbeDefaults.Value;
	public Vector3 position = new(1, 2, 3);
	public ProbeMode mode = ProbeMode.Second;
	public System.Collections.Generic.List<ProbeMode> modes = new() { ProbeMode.First, ProbeMode.Second };

	public override void Awake()
	{
		GameObject.Name = $"Session:{++counter}:{value}";
	}
}

// This fixture intentionally has no identity attribute; its catalog entry simulates .meta discovery.
public sealed class AutoIdentityProbe : Behaviour
{
	public int awakeCount;

	public override void Awake() => awakeCount++;
}

public sealed class LegacySerializedProbe : Behaviour
{
	public int savedValue;

	public override void Awake() => GameObject.Name += "Restored:" + savedValue;
}

// Deliberately violates the ownership contract to exercise the unload fault gate.
public sealed class LeakingProbe : Behaviour
{
	public override void Awake()
	{
		AppDomain.CurrentDomain.ProcessExit += OnProcessExit;
	}

	private static void OnProcessExit(object? sender, EventArgs arguments)
	{
	}
}

public sealed class SpawnerProbe : Behaviour
{
	private bool spawned;

	public override void Update()
	{
		if (spawned) return;
		spawned = true;
		float delta = Time.DeltaTime;
		var added = GameObject.AddComponent<SelfQueryProbe>();
		if (Time.DeltaTime != delta)
			throw new InvalidOperationException("Nested lifecycle dispatch lost the outer frame context.");
		if (!added.awakeSeen || !ReferenceEquals(GameObject.GetComponent<SelfQueryProbe>(), added))
			throw new InvalidOperationException("AddComponent returned before binding and Awake completed.");
		GameObject.Name = "Spawned:Bound:Awake";
	}
}

public sealed class SelfQueryProbe : Behaviour
{
	public bool awakeSeen;

	public override void Awake()
	{
		if (!ReferenceEquals(GameObject.GetComponent<SelfQueryProbe>(), this))
			throw new InvalidOperationException("Awake did not observe its registered Behaviour instance.");
		awakeSeen = true;
	}

	public override void OnEnable()
	{
		_ = Time.DeltaTime;
	}
}

public enum ProbeMode : byte
{
	First = 1,
	Second = 3
}

public sealed class ManagedRenderComponentProbe : Behaviour
{
	public bool verifyTextureBindings;
	public Mesh? mesh;
	public Material? material;
	public Mesh?[]? meshes;
	public System.Collections.Generic.List<Material?>? materials;
	public Vector2 uv;
	public Vector4 tangent;

	public override void Awake()
	{
		if (mesh != null && material != null)
		{
			if (GameObject.GetComponent<MeshFilterComponent>()!.SharedMesh != mesh ||
			    GameObject.GetComponent<MeshRendererComponent>()!.SharedMaterial != material ||
			    meshes == null || meshes[0] != mesh || meshes[1] != null || meshes[2] != mesh ||
			    materials == null || materials[0] != material || materials[1] != null || materials[2] != material ||
			    uv.X != 0.25f || tangent.W != -1)
				throw new InvalidOperationException("Persisted resource fields lost sharing or math values.");
			GameObject.Name = "MeshFields:Restored";
			return;
		}
		GameObject.Name = "RenderProbe:Awake";
		CameraComponent camera = GameObject.AddComponent<CameraComponent>();
		GameObject.Name = "RenderProbe:Camera";
		camera.VerticalFovRadians = 1.1f;
		camera.NearPlane = 0.2f;
		camera.FarPlane = 80.0f;

		var filter = GameObject.AddComponent<MeshFilterComponent>();
		var renderer = GameObject.AddComponent<MeshRendererComponent>();
		mesh = Mesh.Create();
		mesh.SetData(new MeshData
		{
			Positions = new[] { new Vector3(0, 0, 0), new Vector3(1, 0, 0), new Vector3(0, 1, 0) },
			Indices = new uint[] { 0, 1, 2 }
		});
		filter.SharedMesh = mesh;
		var original = mesh;
		mesh = filter.Mesh;
		if (mesh == original || !ReferenceEquals(mesh, filter.Mesh) || filter.SharedMesh != mesh)
			throw new InvalidOperationException("Mesh instance ownership failed.");
		var data = mesh.Vertices;
		data[0].X = 7;
		if (mesh.Vertices[0].X != 0) throw new InvalidOperationException("Mesh getter exposed a live array.");
		mesh.RecalculateNormals();
		mesh.RecalculateBounds();
		material = Material.Create();
		renderer.SharedMaterials = new Material?[] { material, material, null };
		var originalMaterial = material;
		material = renderer.Material!;
		if (material == originalMaterial || !ReferenceEquals(material, renderer.Material))
			throw new InvalidOperationException("Material instance ownership failed.");
		material.Color = new Color(0.25f, 0.5f, 0.75f);
		material.SetColor("_BaseColor", material.Color);
		if (material.GetColor("_BaseColor").B != 0.75f) throw new InvalidOperationException("Shader Color property failed.");
		if (verifyTextureBindings)
		{
		var texture = TextureAsset.Load(new AssetReference("34567890-0000-4000-8000-000000000001"));
		material.SetTexture(MaterialTextureSlot.BaseColor, texture);
		material.SetTexture("_BaseMap", texture);
		if (material.GetTexture(MaterialTextureSlot.BaseColor)?.Reference.Guid != texture.Reference.Guid ||
		    material.GetTexture("_BaseMap")?.Reference.Guid != texture.Reference.Guid)
			throw new InvalidOperationException("Material texture references were not preserved.");
		var before = material.Color;
		bool invalidPropertyRejected = false;
		try { material.SetFloat("Unknown", 1); } catch (InvalidOperationException) { invalidPropertyRejected = true; }
		if (!invalidPropertyRejected || material.Color.R != before.R) throw new InvalidOperationException("Invalid Shader property changed material data.");
		}
		var slots = renderer.Materials;
		if (slots[0] != material || slots[1] == originalMaterial || slots[2] != null || renderer.Materials[1] != slots[1])
			throw new InvalidOperationException("Material slot getter cloned repeatedly.");
		meshes = new Mesh?[] { mesh, null, mesh };
		materials = new System.Collections.Generic.List<Material?> { material, null, material };
		uv = new Vector2(0.25f, 0.75f);
		tangent = new Vector4(1, 0, 0, -1);
		bool duplicateRejected = false;
		try { GameObject.AddComponent<MeshRendererComponent>(); }
		catch (InvalidOperationException) { duplicateRejected = true; }
		if (!duplicateRejected) throw new InvalidOperationException("Duplicate Renderer was accepted.");
		original.Destroy();
		originalMaterial.Destroy();

	}
}

public sealed class DestroyerProbe : Behaviour
{
	public override void Update()
	{
		GameObject created = GameObject.Create("Disposable");
		created.Parent = GameObject;
		created.Transform.Position = new Vector3(4, 5, 6);
		if (created.Transform.Position.X != 4) throw new InvalidOperationException("World position assignment failed.");
		created.Destroy();
	}
}
