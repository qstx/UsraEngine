using Ursa;
using Ursa.Editor;

namespace Ursa.Examples;

// Abstract examples are compiled with the Editor fixtures without registering project extensions.
[CustomEditor(typeof(CameraComponent))]
public abstract class TypedCameraInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		SerializedObject.Update();
		var nearPlane = SerializedObject.FindProperty("nearPlane");
		EditorGui.PropertyField(nearPlane, "Near Plane");
		if (EditorGui.Button("Reset")) nearPlane.DoubleValue = 0.1;
		SerializedObject.ApplyModifiedProperties();
	}
}

[EditorWindowMenu("Tools/Project/Overview")]
public abstract class TypedOverviewWindow : EditorWindow
{
	public override void OnGUI()
	{
		EditorGui.Text("Assets: " + EditorContext.Assets.Length);
		foreach (var selected in EditorContext.Selection)
			EditorGui.Text(selected.Name + " (" + selected.Reference.Kind + ")");
		if (EditorContext.Document is { } document)
			EditorGui.Text(document.Path + (document.Dirty ? " *" : ""));
	}
}
