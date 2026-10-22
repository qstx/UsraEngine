using System;
using Ursa;
using Ursa.Editor;

namespace Ursa.EditorFixtures;

public sealed class EditorOnlyImportAsset : ScriptableObject
{
	public int value = 1;
}

[ScriptedImporter(1, "ursavalidation")]
public sealed class ValidationImporter : ScriptedImporter
{
	private static Mesh? expired;
	public override void OnImportAsset(AssetImportContext context)
	{
		string mode = context.ReadSourceText();
		if (mode.Contains('|'))
		{
			string[] input = mode.Split('|');
			var reference = new AssetReference(input[1], ulong.Parse(input[2], System.Globalization.CultureInfo.InvariantCulture));
			var source = ScriptableObject.Load<Ursa.Examples.GeneratedFormatAsset>(reference);
			bool meshReadOnly = false, materialReadOnly = false;
			try { source.mesh.Clear(); } catch (InvalidOperationException) { meshReadOnly = true; }
			try { source.material.Color = new Color(1, 0, 0, 1); } catch (InvalidOperationException) { materialReadOnly = true; }
			if (!meshReadOnly || !materialReadOnly || source.texture.GetPixels().Length != 2)
				throw new InvalidOperationException("Typed persistent import assets were mutable or unreadable.");
			if (input[0] == "republish-persistent")
			{
				context.AddObjectToAsset("data", source);
				context.SetMainObject(source);
				return;
			}
			if (input[0] == "mutate-persistent") source.size += 1;
			if (input[0] == "destroy-persistent") source.Dispose();
			var cloned = source.mesh.Clone();
			cloned.RecalculateBounds();
			context.AddObjectToAsset("mesh", cloned);
			context.SetMainObject(cloned);
			return;
		}
		if (mode == "editor-type")
		{
			var editorAsset = ScriptableObject.CreateInstance<EditorOnlyImportAsset>();
			context.AddObjectToAsset("data", editorAsset);
			context.SetMainObject(editorAsset);
			return;
		}
		if (mode == "expired")
		{
			context.AddObjectToAsset("mesh", expired!);
			context.SetMainObject(expired!);
			return;
		}
		var mesh = Mesh.Create();
		mesh.SetData(new MeshData
		{
			Positions = new[] { new Vector3(0, 0, 0), new Vector3(1, 0, 0), new Vector3(0, 1, 0) },
			Indices = mode == "bad-index" ? new uint[] { 0, 1, 9 } : new uint[] { 0, 1, 2 },
			SubMeshes = new[] { new SubMesh(0, 3) }
		});
		context.AddObjectToAsset("mesh", mesh);
		if (mode == "unregistered-reference")
		{
			var data = ScriptableObject.CreateInstance<Ursa.Examples.GeneratedFormatAsset>();
			data.texture = new Texture2D(1, 1);
			context.AddObjectToAsset("data", data);
		}
		if (mode == "duplicate-key") context.AddObjectToAsset("mesh", Mesh.Create());
		if (mode == "duplicate-object") context.AddObjectToAsset("other", mesh);
		if (mode != "no-main") context.SetMainObject(mesh);
		if (mode == "destroyed") mesh.Destroy();
		if (mode == "bad-pixels")
		{
			var texture = new Texture2D(1, 1);
			texture.SetPixels(new[] { new Color(float.NaN, 0, 0, 1) });
			context.AddObjectToAsset("texture", texture);
		}
		expired = mesh;
	}
}
