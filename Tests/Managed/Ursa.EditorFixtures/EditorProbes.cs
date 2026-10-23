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
		var typed = (Ursa.SessionFixtures.SessionProbe)Target;
		if (typed.GameObject.Name.Length == 0 || typed.value != SerializedObject.FindProperty("value").IntValue)
			throw new InvalidOperationException("Typed component Target did not bind its owner or field snapshot.");
		if (!ReferenceEquals(typed, typed.GameObject.GetComponent<Ursa.SessionFixtures.SessionProbe>()))
			throw new InvalidOperationException("GetComponent did not return the same scoped Behaviour target.");
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
	private CameraComponent? previous;
	public override void OnInspectorGUI()
	{
		if (previous != null)
		{
			bool expired = false;
			try { _ = previous.Enabled; } catch (InvalidOperationException) { expired = true; }
			if (!expired) throw new InvalidOperationException("A native component target survived its callback.");
		}
		var camera = (CameraComponent)Target;
		if (camera.GameObject.Name.Length == 0) throw new InvalidOperationException("Typed Camera target lost its owner.");
		previous = camera;
		SerializedObject.FindProperty("nearPlane").DoubleValue = 0.25;
		SerializedObject.ApplyModifiedProperties();
	}
}

[CustomEditor(typeof(Material))]
public sealed class TypedMaterialInspector : Inspector
{
	private Material? previous;
	public override void OnInspectorGUI()
	{
		if (previous != null)
		{
			bool expired = false;
			try { _ = previous.GetFloat("_Strength"); } catch (InvalidOperationException) { expired = true; }
			if (!expired) throw new InvalidOperationException("A Material target survived its callback.");
		}
		var material = (Material)Target;
		material.SetFloat("_Strength", 0.75f);
		material.SetColor("_Color", new Color(0.2f, 0.4f, 0.6f, 1));
		previous = material;
		if (EditorGui.Button("Save material target")) EditorContext.Save();
	}
}

[CustomEditor(typeof(GameObject))]
public sealed class PrefabTargetInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		var root = (GameObject)Target;
		root.Name = "Typed Prefab target";
		root.Transform.LocalPosition = new Vector3(2, 3, 4);
		if (root.GetComponent<CameraComponent>() == null) throw new InvalidOperationException("Prefab target did not expose its native components.");
		var script = root.GetComponent<Ursa.SessionFixtures.SessionProbe>() ?? throw new InvalidOperationException("Prefab target did not expose its script component.");
		script.value += 2;
		EditorContext.MarkDirty();
	}
}

[CustomEditor(typeof(TextureImporter))]
public sealed class TextureSettingsInspector : AssetImporterEditor
{
	private PreviewRenderUtility? preview;
	private Texture2D? image;
	private static int enables;
	private static int disables;
	private int settings;
	public override void OnEnable()
	{
		preview = new PreviewRenderUtility();
		image = new Texture2D(2, 2, true);
		image.SetPixels(new[] { new Color(1, 0, 0, .5f), new Color(0, 1, 0), new Color(0, 0, 1, 0), new Color(1, 1, 1) });
		++enables;
	}
	public override void OnDisable() { preview?.Cleanup(); preview = null; ++disables; }
	public override bool HasPreviewGUI() => true;
	public override bool RequiresConstantRepaint() => true;
	public override string GetInfoString() => $"texture preview enables={enables} disables={disables} settings={settings}";
	public override void OnPreviewSettings() { ++settings; EditorGui.Text("Texture preview toolbar"); }
	public override void OnPreviewGUI(Rect rect)
	{
		if (((TextureImporter)Target).AssetPath != "prj:TextureProbe.png") throw new InvalidOperationException("Preview resolved the wrong importer.");
		try { EditorContext.MarkDirty(); throw new Exception("Preview dirtied its author document."); } catch (InvalidOperationException) { }
		EditorGui.DrawTexture(rect, image!);
	}
	private TextureImporter? previous;
	public override void OnInspectorGUI()
	{
		if (previous != null)
		{
			bool expired = false;
			try { _ = previous.AssetPath; } catch (InvalidOperationException) { expired = true; }
			if (!expired) throw new InvalidOperationException("An importer wrapper survived its Editor callback.");
		}
		var importer = (TextureImporter)Target;
		if (importer.AssetPath != "prj:TextureProbe.png") throw new InvalidOperationException("Wrong typed importer target path.");
		importer.MaxDimension = 8;
		previous = importer;
		base.OnInspectorGUI();
	}
}

[CustomEditor(typeof(Ursa.SessionFixtures.ReloadDependency))]
public sealed class SettingsAssetInspector : Inspector
{
	private Texture2D? image;
	public override void OnEnable()
	{
		image = new Texture2D(2, 2, true);
		image.SetPixels(new[] { new Color(1, 0, 0, .5f), new Color(0, 1, 0), new Color(0, 0, 1, 0), new Color(1, 1, 1) });
	}
	public override void OnDisable() { image = null; }
	public override bool HasPreviewGUI() => true;
	public override string GetInfoString() => "ScriptableObject image with alpha";
	public override void OnPreviewGUI(Rect rect)
	{
		if (((Ursa.SessionFixtures.ReloadDependency)Target).value < 0) throw new InvalidOperationException("Injected ScriptableObject Preview failure.");
		EditorGui.DrawTexture(rect, image!);
	}
	private Ursa.SessionFixtures.ReloadDependency? previous;
	public override void OnInspectorGUI()
	{
		if (previous != null && previous.IsValid) throw new InvalidOperationException("A typed source target survived its callback.");
		var asset = (Ursa.SessionFixtures.ReloadDependency)Target;
		if (!asset.IsPersistent || !Nullable.Equals(asset.AssetReference, EditorContext.Target?.AssetReference) ||
		    EditorContext.Target?.Kind != EditorReferenceKind.ScriptableObject || EditorContext.Document != null)
			throw new InvalidOperationException("Typed source target lost its identity or borrowed the active document.");
		asset.value += 2;
		previous = asset;
		EditorContext.Save();
	}
}

[CustomEditor(typeof(Mesh))]
public sealed class MeshPreviewInspector : Inspector
{
	private PreviewRenderUtility? preview;
	private Material? material;
	private bool fail;
	private float yaw;
	private float distance = 3;
	private bool lightEnabled = true;
	public override void OnEnable() { preview = new PreviewRenderUtility(); material = Material.Create(MaterialType.PbrMetallicRoughness); material.Color = new Color(.9f, .05f, .02f); }
	public override void OnDisable() { material?.Destroy(); material = null; preview?.Cleanup(); preview = null; }
	public override bool HasPreviewGUI() => true;
	public override bool RequiresConstantRepaint() => true;
	public override string GetInfoString() => "Private Mesh Preview camera";
	public override void OnPreviewSettings()
	{
		if (EditorGui.Button("Fail next Preview")) fail = true;
		if (EditorGui.Button("Toggle preview light")) lightEnabled = !lightEnabled;
	}
	public override void OnPreviewGUI(Rect rect)
	{
		if (fail) throw new InvalidOperationException("Injected Mesh Preview failure.");
		var input = PreviewInput.Current;
		yaw += input.MouseDelta.X * .02f;
		distance = Math.Clamp(distance - input.ScrollDelta * .2f, 1, 8);
		preview!.Camera.Position = new(MathF.Sin(yaw) * distance, 0, MathF.Cos(yaw) * distance);
		preview.LightIntensity = lightEnabled ? 2 : 0;
		preview.AmbientIntensity = lightEnabled ? .4f : 0;
		preview.BeginPreview(rect);
		preview.DrawMesh((Mesh)Target, material!, scale: new Vector3(1, .7f, .45f));
		preview.EndPreview();
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
