using Ursa;
using System;

namespace Ursa.SessionFixtures;

[UrsaScriptType("test.import-child")]
public sealed class ImportChildAsset : ScriptableObject
{
	public int marker = 42;
}

[UrsaScriptType("test.import-probe")]
public sealed class ImportProbeAsset : ScriptableObject
{
	public Mesh mesh = null!;
	public Material material = null!;
	public Texture2D texture = null!;
	public ImportChildAsset child = null!;
	public int validations;
	public int capabilityRejections;
	public int gameLifecycleCalls;
	protected override void OnEnable()
	{
		mesh = Mesh.Create();
		mesh.SetData(new MeshData
		{
			Positions = new[] { new Vector3(0, 0, 0), new Vector3(1, 0, 0), new Vector3(0, 1, 0) },
			Indices = new uint[] { 0, 1, 2 }, SubMeshes = new[] { new SubMesh(0, 3) }
		});
		material = Material.Create(MaterialType.Unlit);
		material.Color = new Color(0, 1, 0, 1);
		texture = new Texture2D(2, 1);
		texture.SetPixels(new[] { new Color(1, 0, 0, 1), new Color(0, 0, 1, 1) });
		child = CreateInstance<ImportChildAsset>();
		GameObject root = GameObject.Create("ImportRoot");
		GameObject node = GameObject.Create("ImportChild");
		node.Parent = root;
		node.Transform.LocalPosition = new Vector3(1, 2, 3);
		node.AddComponent<MeshFilterComponent>().SharedMesh = mesh;
		node.AddComponent<MeshRendererComponent>().SharedMaterial = material;
		try { Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(null); }
		catch (InvalidOperationException) { ++capabilityRejections; }
		try { node.AddComponent<ImportGameLifecycleProbe>(); }
		catch (InvalidOperationException) { ++capabilityRejections; }
		gameLifecycleCalls = ImportGameLifecycleProbe.calls;
	}
	protected override void OnValidate() => ++validations;
}

public sealed class ImportGameLifecycleProbe : Behaviour
{
	internal static int calls;
	public override void Awake() => ++calls;
	public override void OnEnable() => ++calls;
	public override void OnDestroy() => ++calls;
}
