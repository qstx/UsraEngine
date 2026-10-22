using System;
using Ursa;
using Ursa.Editor;

namespace Ursa.Examples;

public sealed class ModelImportRules : AssetPostprocessor
{
	public override uint GetVersion() => 1;
	public override int GetPostprocessOrder() => 100;
	public override void OnPreprocessModel()
	{
		if (!AssetPath.StartsWith("prj:Models/Processed/", StringComparison.Ordinal)) return;
		var importer = (ModelImporter)AssetImporter;
		importer.ScaleFactor = 0.01f;
		importer.GenerateNormals = true;
		importer.GenerateTangents = true;
	}
	public override void OnPostprocessModel(GameObject root)
	{
		if (!AssetPath.StartsWith("prj:Models/Processed/", StringComparison.Ordinal)) return;
		foreach (RendererComponent renderer in root.GetComponentsInChildren<RendererComponent>(true))
		{
			Material?[] materials = renderer.SharedMaterials;
			for (int index = 0; index < materials.Length; ++index)
			{
				if (materials[index] is not Material source) continue;
				Material copy = source.Clone();
				copy.Roughness = 0.7f;
				materials[index] = copy;
			}
			renderer.SharedMaterials = materials;
		}
		GameObject marker = GameObject.Create("Import marker");
		marker.Parent = root;
		marker.Transform.LocalPosition = new Vector3(0, 1, 0);
	}
}
