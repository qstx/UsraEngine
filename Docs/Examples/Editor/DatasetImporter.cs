using System;
using Ursa.Editor;
using Ursa;
using System.Text.Json;

namespace Ursa.Examples;

[ScriptedImporter(5, "ursadataset")]
public sealed class DatasetImporter : ScriptedImporter
{
	public float scale = 1;
	public override void OnImportAsset(AssetImportContext context)
	{
		if (context.AssetPath != AssetPath || !AssetPath.EndsWith(".ursadataset", StringComparison.Ordinal))
			throw new InvalidOperationException("Importer did not receive its scoped asset path.");
		using JsonDocument input = JsonDocument.Parse(context.ReadSourceText());
		float size = input.RootElement.GetProperty("size").GetSingle() * scale;
		string label = input.RootElement.GetProperty("label").GetString()!;
		if (input.RootElement.TryGetProperty("sourceDependency", out var sourceDependency))
			label = context.ReadSourceAssetText(sourceDependency.GetString()!);
		if (input.RootElement.TryGetProperty("declaredSourceDependency", out var declaredSource))
			context.DependsOnSourceAsset(declaredSource.GetString()!);
		if (input.RootElement.TryGetProperty("artifactGuid", out var artifactGuid))
		{
			var reference = new AssetReference(artifactGuid.GetString()!);
			context.DependsOnArtifact(reference);
			if (context.ReadArtifactBytes(reference).Length == 0) throw new InvalidOperationException("Dependency artifact is empty.");
		}
		if (!float.IsFinite(size) || size <= 0) throw new InvalidOperationException("Size must be finite and positive.");
		var mesh = Mesh.Create();
		mesh.SetData(new MeshData
		{
			Positions = new[] { new Vector3(0, 0, 0), new Vector3(size, 0, 0), new Vector3(0, size, 0) },
			Indices = new uint[] { 0, 1, 2 }, SubMeshes = new[] { new SubMesh(0, 3) }
		});
		var material = Material.Create(MaterialType.Unlit);
		material.Color = new Color(1, 1, 1, 1);
		var texture = new Texture2D(2, 1);
		texture.SetPixels(new[] { new Color(1, 0, 0, 1), new Color(0, 0, 1, 1) });
		material.SetTexture(MaterialTextureSlot.BaseColor, texture);
		material.SetTexture("_BaseMap", texture);
		var data = ScriptableObject.CreateInstance<GeneratedFormatAsset>();
		data.label = label; data.size = size; data.mesh = mesh; data.material = material; data.texture = texture;
		var root = GameObject.Create(label);
		var node = GameObject.Create("Triangle");
		node.Parent = root;
		node.Transform.LocalPosition = new Vector3(1, 2, 3);
		node.AddComponent<MeshFilterComponent>().SharedMesh = mesh;
		node.AddComponent<MeshRendererComponent>().SharedMaterial = material;
		context.AddObjectToAsset("mesh", mesh);
		context.AddObjectToAsset("material", material);
		context.AddObjectToAsset("texture", texture);
		context.AddObjectToAsset("data", data);
		context.AddObjectToAsset("prefab", root);
		context.SetMainObject(input.RootElement.TryGetProperty("main", out var main) && main.GetString() == "data" ? data : root);
		if (input.RootElement.TryGetProperty("warning", out var warning)) context.LogImportWarning(warning.GetString()!);
		if (input.RootElement.TryGetProperty("error", out var error)) context.LogImportError(error.GetString()!);
	}
}
