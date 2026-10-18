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
			var value = SerializedObject.FindProperty("value");
			if (value.PropertyPath != "value") throw new Exception("Field path exposed storage syntax.");
			foreach (string path in SerializedObject.GetPropertyPaths())
				if (path.StartsWith("/", StringComparison.Ordinal)) throw new Exception("Property enumeration exposed storage syntax.");
			foreach (string invalidPath in new[] { "/Fields/value", "position/0", "position..x", "modes.0", "modes.Array.data[-1]", "modes.Array.data[]" })
			{
				try { _ = SerializedObject.FindProperty(invalidPath); throw new Exception("Invalid public property path was accepted."); }
				catch (ArgumentException) { }
			}
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
			var position = SerializedObject.FindProperty("position");
			var positionX = position.FindPropertyRelative("x");
			if (positionX.PropertyPath != "position.x" || positionX.DoubleValue != SerializedObject.FindProperty("position.x").DoubleValue)
				throw new Exception("Relative math component lookup differed from the dotted path.");
			positionX.DoubleValue = 9.0;
			if (position.Vector3Value.X != 9.0f) throw new Exception("Math component edit did not update the parent property.");
			var modes = SerializedObject.FindProperty("modes");
			var secondMode = modes.GetArrayElementAtIndex(1);
			if (secondMode.PropertyPath != "modes.Array.data[1]" || secondMode.EnumULongValue != SerializedObject.FindProperty("modes.Array.data[1]").EnumULongValue ||
				modes.FindPropertyRelative("Array.data[1]").EnumULongValue != secondMode.EnumULongValue)
				throw new Exception("Collection element lookup differed from the Unity-style path.");
			secondMode.EnumULongValue = 1;
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
		SerializedObject.FindProperty("nearPlane").DoubleValue = 0.25;
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
