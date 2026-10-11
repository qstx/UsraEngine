using System;
using System.Threading.Tasks;
using Ursa;
using Ursa.Editor;

namespace Ursa.EditorFixtures;

internal static class Menus
{
	[MenuItem("Tools/Test/Mark Dirty")]
	private static void Dirty() => EditorContext.MarkDirty();

	[MenuItem("Tools/Test/Save")]
	private static void Save() => EditorContext.Save();

	[MenuItem("Tools/Test/Throw")]
	private static void Fail() => throw new InvalidOperationException("Synthetic menu failure");

	[MenuItem("Tools/Test/Thread Guard")]
	private static void CheckThread()
	{
		bool rejected = Task.Run(() =>
		{
			try { EditorGui.Text("invalid background GUI"); return false; }
			catch (InvalidOperationException) { return true; }
		}).GetAwaiter().GetResult();
		if (!rejected) throw new InvalidOperationException("Background Editor API access was accepted.");
	}
}

[CustomInspector("test.session-probe")]
public sealed class ProbeInspector : Inspector
{
	private static SerializedProperty? previousProperty;

	public override void OnInspectorGUI()
	{
		using (EditorGui.Vertical())
		{
			EditorGui.Text(EditorContext.Document.GetProperty("Path").GetString()!);
			EditorGui.TextField("Label", "test");
			EditorGui.FloatField("Float", 1.0);
			EditorGui.IntField("Integer", -1);
			EditorGui.Toggle("Enabled", true);
			EditorGui.Vector3Field("Position", new Vector3(1, 2, 3));
			EditorGui.QuaternionField("Rotation", new Quaternion(0, 0, 0, 1));
			EditorGui.ColorField("Color", new Color(1, 1, 1));
			EditorGui.Popup("Mode", 0, new[] { "First", "Second" });
			// A retained property must never write to a later drawing scope.
			if (previousProperty != null) previousProperty.SetValue(1000L);
			var value = SerializedObject.FindProperty("/Fields/value");
			value.SetValue(value.Value.GetInt64() + 1);
			SerializedObject.ApplyModifiedProperties();
			previousProperty = value;
		}
	}
}

[CustomInspector("ursa.Camera")]
public sealed class CameraInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		SerializedObject.FindProperty("/Fields/nearPlane").SetValue(0.25);
		SerializedObject.ApplyModifiedProperties();
	}
}

[EditorWindowMenu("Tools/Test/Probe Window")]
public sealed class ProbeWindow : EditorWindow
{
	public override void OnGUI()
	{
		EditorGui.Text("Project tool window");
		EditorGui.Text(EditorContext.Selection.GetArrayLength().ToString());
	}
}

[EditorWindowMenu("Tools/Test/Throw Window")]
public sealed class ThrowWindow : EditorWindow
{
	public override void OnGUI()
	{
		throw new InvalidOperationException("Synthetic tool window failure");
	}
}
