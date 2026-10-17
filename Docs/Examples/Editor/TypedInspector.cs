using Ursa.Editor;

namespace Ursa.Examples;

// Abstract examples are compiled with the Editor fixtures without registering project extensions.
[CustomInspector("demo.move-with-input")]
public abstract class TypedMovementInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		SerializedObject.Update();
		var step = SerializedObject.FindProperty("step");
		EditorGui.PropertyField(step, "Step");
		if (EditorGui.Button("Reset")) step.DoubleValue = 1.0;
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
