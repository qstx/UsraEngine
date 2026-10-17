using System;

namespace Ursa.Editor;

public static class EditorGui
{
	public static void Text(string text) { _ = EditorApi.Generation; NativeEditorBindings.Text(text); }
	public static bool Button(string label) { _ = EditorApi.Generation; return NativeEditorBindings.Button(label); }
	public static void SameLine() { _ = EditorApi.Generation; NativeEditorBindings.SameLine(); }
	public static void Separator() { _ = EditorApi.Generation; NativeEditorBindings.Separator(); }
	public static bool Toggle(string label, bool value) { _ = EditorApi.Generation; return NativeEditorBindings.Toggle(label, value); }
	public static long IntField(string label, long value) { _ = EditorApi.Generation; return NativeEditorBindings.IntField(label, value); }
	public static double FloatField(string label, double value) { _ = EditorApi.Generation; return NativeEditorBindings.FloatField(label, value); }
	public static string TextField(string label, string value) { _ = EditorApi.Generation; return NativeEditorBindings.TextField(label, value); }
	public static int Popup(string label, int selected, string[] options) { _ = EditorApi.Generation; return NativeEditorBindings.Popup(label, selected, options); }
	public static Vector2 Vector2Field(string label, Vector2 value) { _ = EditorApi.Generation; return NativeEditorBindings.Vector2Field(label, value); }
	public static Vector3 Vector3Field(string label, Vector3 value) { _ = EditorApi.Generation; return NativeEditorBindings.Vector3Field(label, value); }
	public static Vector4 Vector4Field(string label, Vector4 value) { _ = EditorApi.Generation; return NativeEditorBindings.Vector4Field(label, value); }
	public static Quaternion QuaternionField(string label, Quaternion value) { _ = EditorApi.Generation; return NativeEditorBindings.QuaternionField(label, value); }
	public static Color ColorField(string label, Color value) { _ = EditorApi.Generation; return NativeEditorBindings.ColorField(label, value); }
	public static EditorReference? ObjectField(string label, EditorReference? value, EditorReferenceKind kind)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.ObjectField(label, value?.value, kind) is { } edited ? new(edited) : null;
	}
	public static EditorReference? AssetField(string label, EditorReference? value, EditorReferenceKind kind = EditorReferenceKind.Asset)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.AssetField(label, value?.value, kind) is { } edited ? new(edited) : null;
	}
	public static void PropertyField(SerializedProperty property, string? label = null)
	{
		ArgumentNullException.ThrowIfNull(property);
		property.RequireValid();
		NativeEditorBindings.PropertyField(property.Revision, property.Path, label ?? property.Path);
	}
	public static IDisposable Vertical()
	{
		_ = EditorApi.Generation;
		NativeEditorBindings.BeginGroup();
		return new LayoutScope(EditorApi.Generation);
	}
	private sealed class LayoutScope(ulong generation) : IDisposable
	{
		private bool disposed;
		public void Dispose()
		{
			if (disposed) return;
			EditorApi.RequireGeneration(generation);
			NativeEditorBindings.EndGroup();
			disposed = true;
		}
	}
}
