using System;

namespace Ursa.Editor;

public static class EditorGui
{
	public static void DrawTexture(Rect rect, Texture texture)
	{
		ArgumentNullException.ThrowIfNull(texture);
		if (texture is Texture2D { EditorStorage: { } pixels })
		{
			pixels.RequireValid();
			NativeEditorBindings.DrawPreviewPixels(rect, checked((uint)pixels.Width), checked((uint)pixels.Height), pixels.Pixels, pixels.Linear);
			return;
		}
		EditorApi.EnsureRuntimeScope();
		NativeEditorBindings.DrawPreviewTexture(rect, texture.Handle);
	}
	public static void DrawPreviewPixels(Rect rect, int width, int height, Color[] pixels, bool linear = false)
	{
		_ = EditorApi.Generation;
		if (width <= 0 || height <= 0) throw new ArgumentOutOfRangeException(nameof(width));
		NativeEditorBindings.DrawPreviewPixels(rect, checked((uint)width), checked((uint)height), pixels ?? throw new ArgumentNullException(nameof(pixels)), linear);
	}
	public static void Text(string text) { _ = EditorApi.Generation; NativeEditorBindings.Text(text); }
	public static bool Button(string label) { _ = EditorApi.Generation; return NativeEditorBindings.Button(label); }
	public static bool SmallButton(string label) { _ = EditorApi.Generation; return NativeEditorBindings.SmallButton(label); }
	public static void SameLine() { _ = EditorApi.Generation; NativeEditorBindings.SameLine(); }
	public static void Separator() { _ = EditorApi.Generation; NativeEditorBindings.Separator(); }
	public static bool Toggle(string label, bool value, bool mixedValue = false)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.Toggle(label, value, mixedValue);
	}
	public static long IntField(string label, long value) { _ = EditorApi.Generation; return NativeEditorBindings.IntField(label, value); }
	public static ulong ULongField(string label, ulong value) { _ = EditorApi.Generation; return NativeEditorBindings.ULongField(label, value); }
	public static double FloatField(string label, double value) { _ = EditorApi.Generation; return NativeEditorBindings.FloatField(label, value); }
	public static double SliderFloat(string label, double value, double minimum, double maximum)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.SliderFloat(label, value, minimum, maximum);
	}
	public static double DragFloat(string label, double value, double speed, double minimum, double maximum,
		string format = "%.3f", bool mixedValue = false)
	{
		_ = EditorApi.Generation;
		ArgumentException.ThrowIfNullOrEmpty(format);
		return NativeEditorBindings.DragFloat(label, value, speed, minimum, maximum, format, mixedValue);
	}
	public static string TextField(string label, string value) { _ = EditorApi.Generation; return NativeEditorBindings.TextField(label, value); }
	public static int Popup(string label, int selected, string[] options, bool mixedValue = false)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.Popup(label, selected, options, mixedValue);
	}
	public static Vector2 Vector2Field(string label, Vector2 value) { _ = EditorApi.Generation; return NativeEditorBindings.Vector2Field(label, value); }
	public static Vector3 Vector3Field(string label, Vector3 value) { _ = EditorApi.Generation; return NativeEditorBindings.Vector3Field(label, value); }
	public static Vector4 Vector4Field(string label, Vector4 value) { _ = EditorApi.Generation; return NativeEditorBindings.Vector4Field(label, value); }
	public static Quaternion QuaternionField(string label, Quaternion value) { _ = EditorApi.Generation; return NativeEditorBindings.QuaternionField(label, value); }
	public static Color ColorField(string label, Color value, bool mixedValue = false, bool hdr = false)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.ColorField(label, value, mixedValue, hdr);
	}
	public static EditorReference? ObjectField(string label, EditorReference? value, EditorReferenceKind kind,
		string expectedType = "")
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.ObjectField(label, value?.value, kind, expectedType) is { } edited ? new(edited) : null;
	}
	public static EditorReference? AssetField(string label, EditorReference? value,
		EditorReferenceKind kind = EditorReferenceKind.Asset, string expectedType = "")
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.AssetField(label, value?.value, kind, expectedType) is { } edited ? new(edited) : null;
	}
	private static bool SameReference(EditorReference? left, EditorReference? right)
	{
		if (left == null || right == null) return left == null && right == null;
		if (left.Kind != right.Kind || !StringComparer.Ordinal.Equals(left.PersistentId, right.PersistentId)) return false;
		AssetReference? leftAsset = left.AssetReference;
		AssetReference? rightAsset = right.AssetReference;
		return leftAsset == null || rightAsset == null
			? leftAsset == null && rightAsset == null
			: StringComparer.OrdinalIgnoreCase.Equals(leftAsset.Value.Guid, rightAsset.Value.Guid) &&
			  leftAsset.Value.LocalFileId == rightAsset.Value.LocalFileId;
	}
	public static void PropertyField(SerializedProperty property, string? label = null)
	{
		ArgumentNullException.ThrowIfNull(property);
		property.RequireValid();
		string caption = label ?? property.DisplayName;
		switch (property.PropertyType)
		{
			case SerializedPropertyType.Boolean:
				if (property.HasMultipleDifferentValues) goto default;
				bool originalBoolean = property.BoolValue;
				bool editedBoolean = Toggle(caption, originalBoolean);
				if (editedBoolean != originalBoolean) property.BoolValue = editedBoolean;
				break;
			case SerializedPropertyType.Integer:
				if (property.HasMultipleDifferentValues) goto default;
				long originalInteger = property.LongValue;
				long editedInteger = IntField(caption, originalInteger);
				if (editedInteger != originalInteger) property.LongValue = editedInteger;
				break;
			case SerializedPropertyType.UnsignedInteger:
				if (property.HasMultipleDifferentValues) goto default;
				ulong originalUnsigned = property.ULongValue;
				ulong editedUnsigned = ULongField(caption, originalUnsigned);
				if (editedUnsigned != originalUnsigned) property.ULongValue = editedUnsigned;
				break;
			case SerializedPropertyType.Float:
				if (property.HasMultipleDifferentValues) goto default;
				double originalFloat = property.DoubleValue;
				double editedFloat = property.HasRange
					? SliderFloat(caption, originalFloat, property.Minimum, property.Maximum)
					: FloatField(caption, originalFloat);
				if (editedFloat != originalFloat) property.DoubleValue = editedFloat;
				break;
			case SerializedPropertyType.String:
				if (property.HasMultipleDifferentValues) goto default;
				string originalText = property.StringValue ?? string.Empty;
				string editedText = TextField(caption, originalText);
				if (!StringComparer.Ordinal.Equals(editedText, originalText)) property.StringValue = editedText;
				break;
			case SerializedPropertyType.Enum:
				int originalIndex = property.HasMultipleDifferentValues ? -1 : property.EnumValueIndex;
				int editedIndex = Popup(caption, originalIndex, property.EnumNames, property.HasMultipleDifferentValues);
				if (editedIndex >= 0 && editedIndex != originalIndex) property.EnumValueIndex = editedIndex;
				break;
			case SerializedPropertyType.Vector2:
				if (property.HasMultipleDifferentValues) goto default;
				Vector2 originalVector2 = property.Vector2Value;
				Vector2 editedVector2 = Vector2Field(caption, originalVector2);
				if (!editedVector2.Equals(originalVector2)) property.Vector2Value = editedVector2;
				break;
			case SerializedPropertyType.Vector3:
				if (property.HasMultipleDifferentValues) goto default;
				Vector3 originalVector3 = property.Vector3Value;
				Vector3 editedVector3 = Vector3Field(caption, originalVector3);
				if (!editedVector3.Equals(originalVector3)) property.Vector3Value = editedVector3;
				break;
			case SerializedPropertyType.Vector4:
				if (property.HasMultipleDifferentValues) goto default;
				Vector4 originalVector4 = property.Vector4Value;
				Vector4 editedVector4 = Vector4Field(caption, originalVector4);
				if (!editedVector4.Equals(originalVector4)) property.Vector4Value = editedVector4;
				break;
			case SerializedPropertyType.Quaternion:
				if (property.HasMultipleDifferentValues) goto default;
				Quaternion originalQuaternion = property.QuaternionValue;
				Quaternion editedQuaternion = QuaternionField(caption, originalQuaternion);
				if (!editedQuaternion.Equals(originalQuaternion)) property.QuaternionValue = editedQuaternion;
				break;
			case SerializedPropertyType.Color:
				if (property.HasMultipleDifferentValues) goto default;
				Color originalColor = property.ColorValue;
				Color editedColor = ColorField(caption, originalColor, hdr: property.Hdr);
				if (!editedColor.Equals(originalColor)) property.ColorValue = editedColor;
				break;
			case SerializedPropertyType.ObjectReference:
				if (property.HasMultipleDifferentValues) goto default;
				EditorReference? originalObject = property.ObjectReferenceValue;
				EditorReference? editedObject = ObjectField(caption, originalObject, property.ReferenceKind, property.TypeName);
				if (!SameReference(editedObject, originalObject)) property.ObjectReferenceValue = editedObject;
				break;
			case SerializedPropertyType.AssetReference:
				if (property.HasMultipleDifferentValues) goto default;
				EditorReference? originalAsset = property.AssetReferenceValue;
				EditorReference? editedAsset = AssetField(caption, originalAsset, property.ReferenceKind, property.TypeName);
				if (!SameReference(editedAsset, originalAsset)) property.AssetReferenceValue = editedAsset;
				break;
			case SerializedPropertyType.Array:
			case SerializedPropertyType.List:
			{
				int originalSize = property.ArraySize;
				long editedSize = IntField(caption, originalSize);
				if (editedSize >= 0 && editedSize <= int.MaxValue && editedSize != originalSize)
					property.ArraySize = (int)editedSize;
				for (int index = 0; index < property.ArraySize; ++index)
				{
					string elementIndex = index.ToString(System.Globalization.CultureInfo.InvariantCulture);
					string elementId = property.PropertyPath + ".Array.data[" + elementIndex + "]";
					if (index > 0 && SmallButton("^##" + elementId + ".up"))
					{
						property.MoveArrayElement(index, index - 1);
						return;
					}
					if (index + 1 < property.ArraySize)
					{
						SameLine();
						if (SmallButton("v##" + elementId + ".down"))
						{
							property.MoveArrayElement(index, index + 1);
							return;
						}
					}
					SameLine();
					SerializedProperty element = property.GetArrayElementAtIndex(index);
					PropertyField(element, "Element " + index + "##" + elementId + ".value");
				}
				break;
			}
			default:
				NativeEditorBindings.PropertyField(property.Revision, property.StoragePath, caption);
				break;
		}
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
