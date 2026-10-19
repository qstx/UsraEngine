using Ursa.Editor;

[CustomEditor(typeof(MoveWithInputBehaviour))]
public sealed class MoveWithInputInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		EditorGui.Text("Right Arrow moves this object.");
		var step = SerializedObject.FindProperty("step");
		EditorGui.PropertyField(step, "Step");
		if (EditorGui.Button("Reset Step"))
		{
			step.DoubleValue = 1.0;
		}
		SerializedObject.ApplyModifiedProperties();
	}
}

public static class DemoEditorMenus
{
	[MenuItem("Tools/Demo/Save Current Asset")]
	private static void SaveCurrentAsset()
	{
		if (EditorContext.Document != null)
		{
			EditorContext.Save();
		}
	}
}

[EditorWindowMenu("Tools/Demo/Project Overview")]
public sealed class ProjectOverviewWindow : EditorWindow
{
	public override string Title => "Demo Project Overview";

	public override void OnGUI()
	{
		EditorGui.Text("Assets: " + EditorContext.Assets.Length);
		foreach (var selected in EditorContext.Selection)
		{
			EditorGui.Text(selected.Name);
		}
	}
}
