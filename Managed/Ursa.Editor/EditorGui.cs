using System;
using System.Text.Json;
using Ursa;

namespace Ursa.Editor;

public static class EditorGui
{
	public static void Text(string text) => EditorApi.Call("Text", new { Text = text });

	public static bool Button(string label) => EditorApi.Call("Button", new { Label = label }).GetBoolean();

	public static void SameLine() => EditorApi.Call("SameLine");

	public static void Separator() => EditorApi.Call("Separator");

	public static bool Toggle(string label, bool value) => EditorApi.Call("Toggle", new { Label = label, Value = value }).GetBoolean();

	public static long IntField(string label, long value) => EditorApi.Call("IntField", new { Label = label, Value = value }).GetInt64();

	public static double FloatField(string label, double value) => EditorApi.Call("FloatField", new { Label = label, Value = value }).GetDouble();

	public static string TextField(string label, string value) => EditorApi.Call("TextField", new { Label = label, Value = value }).GetString() ?? string.Empty;

	public static int Popup(string label, int selected, string[] options) =>
		EditorApi.Call("Popup", new { Label = label, Value = selected, Options = options }).GetInt32();

	public static Vector3 Vector3Field(string label, Vector3 value)
	{
		JsonElement result = EditorApi.Call("VectorField", new { Label = label, Value = new[] { value.X, value.Y, value.Z } });
		return new Vector3(result[0].GetSingle(), result[1].GetSingle(), result[2].GetSingle());
	}

	public static Quaternion QuaternionField(string label, Quaternion value)
	{
		JsonElement result = EditorApi.Call("VectorField", new { Label = label, Value = new[] { value.X, value.Y, value.Z, value.W } });
		return new Quaternion(result[0].GetSingle(), result[1].GetSingle(), result[2].GetSingle(), result[3].GetSingle());
	}

	public static Color ColorField(string label, Color value)
	{
		JsonElement result = EditorApi.Call("ColorField", new { Label = label, Value = new[] { value.R, value.G, value.B, value.A } });
		return new Color(result[0].GetSingle(), result[1].GetSingle(), result[2].GetSingle(), result[3].GetSingle());
	}

	public static JsonElement ObjectField(string label, JsonElement value, string kind) =>
		EditorApi.Call("ObjectField", new { Label = label, Value = value, Kind = kind });

	public static JsonElement AssetField(string label, JsonElement value) =>
		EditorApi.Call("AssetField", new { Label = label, Value = value });

	public static void PropertyField(SerializedProperty property, string? label = null)
	{
		property.RequireValid();
		EditorApi.Call("PropertyField", new { Path = property.Path, Label = label ?? property.Path });
	}

	public static IDisposable Vertical()
	{
		EditorApi.Call("BeginGroup");
		return new LayoutScope(EditorApi.Generation);
	}

	private sealed class LayoutScope : IDisposable
	{
		private readonly ulong generation;
		private bool disposed;

		internal LayoutScope(ulong generation) { this.generation = generation; }

		public void Dispose()
		{
			if (disposed) return;
			EditorApi.RequireGeneration(generation);
			EditorApi.Call("EndGroup");
			disposed = true;
		}
	}
}
