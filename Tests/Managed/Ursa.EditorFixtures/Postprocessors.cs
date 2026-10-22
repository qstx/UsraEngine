using System;
using System.Globalization;
using Ursa;
using Ursa.Editor;

namespace Ursa.EditorFixtures;

public sealed class FirstTexturePostprocessor : AssetPostprocessor
{
	public override uint GetVersion() => 7;
	public override int GetPostprocessOrder() => -10;
	public override void OnPreprocessTexture()
	{
		if (!AssetPath.EndsWith("Processed.png", StringComparison.Ordinal)) return;
		var importer = (TextureImporter)AssetImporter;
		importer.MaxDimension = 2;
		importer.SRgb = AssetPath.EndsWith("ColorProcessed.png", StringComparison.Ordinal);
		importer.GenerateMipmaps = true;
		importer.FilterMode = TextureFilterMode.Point;
		importer.WrapMode = TextureWrapMode.Clamp;
	}
}

public sealed class TextureAndModelPostprocessor : AssetPostprocessor
{
	private float fill;
	private string modelMode = "valid";
	public override int GetPostprocessOrder() => 10;
	public override void OnPreprocessTexture()
	{
		if (!AssetPath.EndsWith("Processed.png", StringComparison.Ordinal)) return;
		if (((TextureImporter)AssetImporter).MaxDimension != 2) throw new InvalidOperationException("Preprocess order differs.");
		string configuration = Context.ReadSourceAssetText("prj:PixelConfig.txt");
		if (configuration == "error") { Context.LogImportError("Texture rule rejected its configuration."); return; }
		fill = float.Parse(configuration, CultureInfo.InvariantCulture);
	}
	public override void OnPostprocessTexture(Texture2D texture)
	{
		if (!AssetPath.EndsWith("Processed.png", StringComparison.Ordinal)) return;
		if (texture.Width != 2 || texture.Height != 2 || texture.IsLinear == AssetPath.EndsWith("ColorProcessed.png", StringComparison.Ordinal)) throw new InvalidOperationException("Effective texture settings were not applied.");
		var pixels = texture.GetPixels();
		for (int index = 0; index < pixels.Length; ++index) pixels[index] = new Color(fill, 0, 0, 0.5f);
		texture.SetPixels(pixels);
	}
	public override void OnPostprocessModel(GameObject root)
	{
		if (!AssetPath.EndsWith("Processed.fbx", StringComparison.Ordinal)) return;
		if (modelMode == "destroy-root") { root.Destroy(); return; }
		if (modelMode == "remove-joints") { foreach (var child in root.GetChildren()) child.Destroy(); return; }
		if (((ModelImporter)AssetImporter).ScaleFactor != 2) throw new InvalidOperationException("Model preprocess settings were not shared.");
		root.Name = "Processed model";
		root.Transform.LocalPosition = new Vector3(3, 4, 5);
		foreach (var filter in root.GetComponentsInChildren<MeshFilterComponent>(true))
		{
			var vertices = filter.SharedMesh!.Vertices;
			vertices[0].Z += 1;
			filter.SharedMesh.Vertices = vertices;
		}
		foreach (var renderer in root.GetComponentsInChildren<SkinnedMeshRendererComponent>(true))
		{
			if (modelMode == "bad-mesh") renderer.SharedMesh!.Indices = new uint[] { uint.MaxValue, uint.MaxValue, uint.MaxValue };
			var vertices = renderer.SharedMesh!.Vertices;
			vertices[0].Z += 1;
			renderer.SharedMesh.Vertices = vertices;
		}
		foreach (var renderer in root.GetComponentsInChildren<RendererComponent>(true))
		{
			if (renderer.SharedMaterial is Material source && Math.Abs(source.GetFloat("_Roughness") - source.Roughness) > 0.0001f)
				throw new InvalidOperationException("Imported material shader metadata lost its source parameters.");
			var material = renderer.SharedMaterial?.Clone() ?? Material.Create();
			material.Color = new Color(0, 1, 0, 1);
			renderer.SharedMaterials = new Material[] { material, null!, material };
		}
		var added = GameObject.Create("Added model node");
		added.Parent = root;
		added.Transform.LocalPosition = new Vector3(7, 8, 9);
		var removed = GameObject.Create("Deleted model node");
		removed.Parent = root;
		removed.Destroy();
		try { root.AddComponent<ModelLifecycleProbe>(); }
		catch (InvalidOperationException) { }
		if (ModelLifecycleProbe.Calls != 0) throw new InvalidOperationException("Model import executed game lifecycle callbacks.");
		if (modelMode == "post-error") Context.LogImportError("Model postprocessor rejected the candidate.");
	}
	public override void OnPreprocessModel()
	{
		if (!AssetPath.EndsWith("Processed.fbx", StringComparison.Ordinal)) return;
		modelMode = Context.ReadSourceAssetText("prj:ModelConfig.txt");
		if (modelMode == "pre-error") Context.LogImportError("Model preprocessor rejected its settings.");
	}
}

public sealed class LastModelPostprocessor : AssetPostprocessor
{
	public override uint GetVersion() => 2;
	public override int GetPostprocessOrder() => 10;
	public override void OnPreprocessModel()
	{
		if (AssetPath.EndsWith("Processed.fbx", StringComparison.Ordinal)) ((ModelImporter)AssetImporter).ScaleFactor = 2;
	}
	public override void OnPostprocessTexture(Texture2D texture)
	{
		if (!AssetPath.EndsWith("Processed.png", StringComparison.Ordinal)) return;
		var pixels = texture.GetPixels();
		for (int index = 0; index < pixels.Length; ++index)
		{
			if (pixels[index].R <= 0) throw new InvalidOperationException("Stable postprocess order differs.");
			pixels[index] = new Color(pixels[index].R, 1, 0, pixels[index].A);
		}
		texture.SetPixels(pixels);
	}
}

public sealed class ModelLifecycleProbe : Behaviour
{
	public static int Calls;
	public override void Awake() => ++Calls;
	public override void OnEnable() => ++Calls;
	public override void OnDestroy() => ++Calls;
}
