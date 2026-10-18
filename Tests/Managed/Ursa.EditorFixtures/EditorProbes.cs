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

[CustomEditor(typeof(Ursa.SessionFixtures.SessionProbe))]
public sealed class ProbeInspector : Inspector
{
	private static SerializedProperty? previousProperty;

	public override void OnInspectorGUI()
	{
		using (EditorGui.Vertical())
		{
			EditorGui.Text(EditorContext.Document!.Path);
			EditorGui.TextField("Label", "test");
			if (EditorGui.TextField("Embedded NUL", "test\0尾") != "test\0尾") throw new InvalidOperationException("Untouched TextField changed the original string.");
			EditorGui.FloatField("Float", 1.0);
			EditorGui.IntField("Integer", -1);
			EditorGui.Toggle("Enabled", true);
			EditorGui.Vector2Field("UV", new Vector2(1, 2));
			EditorGui.Vector3Field("Position", new Vector3(1, 2, 3));
			EditorGui.Vector4Field("Direction", new Vector4(1, 2, 3, 4));
			EditorGui.QuaternionField("Rotation", new Quaternion(0, 0, 0, 1));
			EditorGui.ColorField("Color", new Color(1, 1, 1));
			EditorGui.Popup("Mode", 0, new[] { "First", "Second" });
			EditorGui.ObjectField("Object", EditorContext.Selection[0].Reference, EditorReferenceKind.GameObject);
			EditorGui.AssetField("Asset", null);
			if (EditorContext.Target?.Kind != EditorReferenceKind.Component) throw new InvalidOperationException("Inspector target kind was lost.");
			// A retained property must never write to a later drawing scope.
			if (previousProperty != null) previousProperty.LongValue = 1000L;
			var value = SerializedObject.FindProperty("/Fields/value");
			try { _ = value.BoolValue; throw new Exception("Mismatched accessor was accepted."); }
			catch (InvalidOperationException) { }
			long before = value.LongValue;
			value.LongValue = -99;
			if (!SerializedObject.HasModifiedProperties) throw new InvalidOperationException("Pending property edit was not tracked.");
			SerializedObject.Update();
			try { _ = value.LongValue; throw new Exception("Old property revision was accepted."); }
			catch (InvalidOperationException) { }
			value = SerializedObject.FindProperty("value");
			if (value.LongValue != before || SerializedObject.HasModifiedProperties || SerializedObject.ApplyModifiedProperties()) throw new InvalidOperationException("Update changed or applied pending data.");
			value.LongValue += 1;
			SerializedObject.ApplyModifiedProperties();
			previousProperty = value;
		}
	}
}

[CustomEditor(typeof(CameraComponent))]
public sealed class CameraInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		SerializedObject.FindProperty("/Fields/nearPlane").DoubleValue = 0.25;
		SerializedObject.ApplyModifiedProperties();
	}
}

[EditorWindowMenu("Tools/Test/Probe Window")]
public sealed class ProbeWindow : EditorWindow
{
	public override void OnGUI()
	{
		EditorGui.Text("Project tool window");
		EditorGui.Text(EditorContext.Selection.Length.ToString());
	}
}

[EditorWindowMenu("Tools/Test/Throw Window")]
public sealed class ThrowWindow : EditorWindow
{
	public override void OnGUI()
	{
		_ = EditorGui.Vertical(); // Native scope cleanup must balance even an undisposed layout after an exception.
		throw new InvalidOperationException("Synthetic tool window failure");
	}
}
