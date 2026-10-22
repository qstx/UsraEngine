using System;
using Ursa;
using Ursa.Editor;

namespace Ursa.Examples;

// Place in Assets/Editor beside DatasetImporterEditor.cs.
[CustomEditor(typeof(GeneratedFormatAsset))]
public sealed class GeneratedFormatAssetEditor : Inspector
{
	private PreviewRenderUtility? preview;
	private bool showImage = true;
	private float yaw;
	public override void OnEnable() => preview = new PreviewRenderUtility();
	public override void OnDisable() { preview?.Cleanup(); preview = null; }
	public override void OnInspectorGUI()
	{
		DrawDefaultInspector();
		showImage = EditorGui.Toggle("Texture preview", showImage);
	}
	public override bool HasPreviewGUI() => true;
	public override string GetInfoString() => showImage ? "Imported texture (RGBA)" : "Generated mesh and material";
	public override void OnPreviewSettings() => showImage = EditorGui.Toggle("Texture", showImage);
	public override void OnInteractivePreviewGUI(Rect rect)
	{
		var data = (GeneratedFormatAsset)Target;
		if (showImage) { EditorGui.DrawTexture(rect, data.texture); return; }
		yaw += PreviewInput.Current.MouseDelta.X * .01f;
		float size = Math.Max(.1f, data.size);
		preview!.Camera.Target = new(size * .5f, size * .5f, 0);
		preview.Camera.Position = new(size * (.5f + 2 * MathF.Sin(yaw)), size * .5f, size * 2 * MathF.Cos(yaw));
		preview.BeginPreview(rect);
		preview.DrawMesh(data.mesh, data.material);
		preview.EndPreview();
	}
}
