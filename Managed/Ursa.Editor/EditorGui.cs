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
	public static void TextWithTooltip(string text, string tooltip)
	{
		_ = EditorApi.Generation;
		NativeEditorBindings.TextWithTooltip(text, tooltip);
	}
	public static bool Button(string label) { _ = EditorApi.Generation; return NativeEditorBindings.Button(label); }
	public static bool SmallButton(string label) { _ = EditorApi.Generation; return NativeEditorBindings.SmallButton(label); }
	public static bool BeginPopupButton(string label, string popupId)
	{
		_ = EditorApi.Generation;
		ArgumentException.ThrowIfNullOrEmpty(popupId);
		return NativeEditorBindings.BeginPopupButton(label, popupId);
	}
	public static void EndPopup() { _ = EditorApi.Generation; NativeEditorBindings.EndPopup(); }
	public static bool BeginMenu(string label) { _ = EditorApi.Generation; return NativeEditorBindings.BeginMenu(label); }
	public static void EndMenu() { _ = EditorApi.Generation; NativeEditorBindings.EndMenu(); }
	public static bool MenuItem(string label, bool enabled = true)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.MenuItem(label, enabled);
	}
	public static int DragDropArrayElement(string id, int index, int count)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.DragDropArrayElement(id, index, count);
	}
	public static void SameLine() { _ = EditorApi.Generation; NativeEditorBindings.SameLine(); }
	public static void SameLineAtContentX(double offsetFromContentX)
	{
		_ = EditorApi.Generation;
		if (!double.IsFinite(offsetFromContentX) || offsetFromContentX < 0)
			throw new ArgumentOutOfRangeException(nameof(offsetFromContentX));
		NativeEditorBindings.SameLineAtContentX(offsetFromContentX);
	}
	public static double FontSize { get { _ = EditorApi.Generation; return NativeEditorBindings.GetFontSize(); } }
	public static void NewLine() { _ = EditorApi.Generation; NativeEditorBindings.NewLine(); }
	public static void Separator() { _ = EditorApi.Generation; NativeEditorBindings.Separator(); }
	public static bool FoldoutHeader(string label) { _ = EditorApi.Generation; return NativeEditorBindings.FoldoutHeader(label); }
	public static void EndFoldout() { _ = EditorApi.Generation; NativeEditorBindings.EndFoldout(); }
	private static bool WasLastControlEdited() { _ = EditorApi.Generation; return NativeEditorBindings.IsLastItemEdited(); }
	public static bool IsLastControlEdited => WasLastControlEdited();
	public static bool IsLastControlActive() { _ = EditorApi.Generation; return NativeEditorBindings.IsLastItemActive(); }
	public static bool Toggle(string label, bool value, bool mixedValue = false)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.Toggle(label, value, mixedValue);
	}
	public static bool ToggleLeft(string label, bool value, bool mixedValue = false)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.ToggleLeft(label, value, mixedValue);
	}
	public static void SetNextItemWidth(double width)
	{
		_ = EditorApi.Generation;
		if (!double.IsFinite(width)) throw new ArgumentOutOfRangeException(nameof(width));
		NativeEditorBindings.SetNextItemWidth(width);
	}
	public static double ContentRegionAvailableWidth
	{
		get { _ = EditorApi.Generation; return NativeEditorBindings.GetContentRegionAvailableWidth(); }
	}
	public static void Indent(double width)
	{
		_ = EditorApi.Generation;
		if (!double.IsFinite(width) || width < 0) throw new ArgumentOutOfRangeException(nameof(width));
		NativeEditorBindings.Indent(width);
	}
	public static void Unindent(double width)
	{
		_ = EditorApi.Generation;
		if (!double.IsFinite(width) || width < 0) throw new ArgumentOutOfRangeException(nameof(width));
		NativeEditorBindings.Unindent(width);
	}
	public static long IntField(string label, long value, bool mixedValue = false) { _ = EditorApi.Generation; return NativeEditorBindings.IntField(label, value, mixedValue); }
	public static ulong ULongField(string label, ulong value, bool mixedValue = false) { _ = EditorApi.Generation; return NativeEditorBindings.ULongField(label, value, mixedValue); }
	public static double FloatField(string label, double value, bool mixedValue = false) { _ = EditorApi.Generation; return NativeEditorBindings.FloatField(label, value, mixedValue); }
	public static double SliderFloat(string label, double value, double minimum, double maximum, bool mixedValue = false)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.SliderFloat(label, value, minimum, maximum, mixedValue);
	}
	public static double DragFloat(string label, double value, double speed, double minimum, double maximum,
		string format = "%.3f", bool mixedValue = false)
	{
		_ = EditorApi.Generation;
		ArgumentException.ThrowIfNullOrEmpty(format);
		return NativeEditorBindings.DragFloat(label, value, speed, minimum, maximum, format, mixedValue);
	}
	public static string TextField(string label, string value, bool mixedValue = false) { _ = EditorApi.Generation; return NativeEditorBindings.TextField(label, value, mixedValue); }
	public static int Popup(string label, int selected, string[] options, bool mixedValue = false)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.Popup(label, selected, options, mixedValue);
	}
	public static Vector2 Vector2Field(string label, Vector2 value, bool mixedValue = false) { _ = EditorApi.Generation; return NativeEditorBindings.Vector2Field(label, value, mixedValue); }
	public static Vector3 Vector3Field(string label, Vector3 value, bool mixedValue = false) { _ = EditorApi.Generation; return NativeEditorBindings.Vector3Field(label, value, mixedValue); }
	public static Vector4 Vector4Field(string label, Vector4 value, bool mixedValue = false) { _ = EditorApi.Generation; return NativeEditorBindings.Vector4Field(label, value, mixedValue); }
	public static Quaternion QuaternionField(string label, Quaternion value, bool mixedValue = false) { _ = EditorApi.Generation; return NativeEditorBindings.QuaternionField(label, value, mixedValue); }
	public static void EulerAnglesField(string label, SerializedProperty property)
	{
		ArgumentNullException.ThrowIfNull(property);
		if (property.PropertyType != SerializedPropertyType.Quaternion)
			throw new ArgumentException("Euler angle fields require a Quaternion property.", nameof(property));
		Quaternion original = property.QuaternionValue;
		Vector3 euler = ToEulerAngles(original);
		Vector3 edited = Vector3Field(label, euler, property.HasMultipleDifferentValues);
		if (!edited.Equals(euler) || property.HasMultipleDifferentValues && WasLastControlEdited())
			property.QuaternionValue = FromEulerAngles(edited);
	}
	private static Vector3 ToEulerAngles(Quaternion rotation)
	{
		double lengthSquared = (double)rotation.X * rotation.X + (double)rotation.Y * rotation.Y +
		                       (double)rotation.Z * rotation.Z + (double)rotation.W * rotation.W;
		if (!double.IsFinite(lengthSquared) || lengthSquared <= 1e-12) return new();
		double inverseLength = 1.0 / Math.Sqrt(lengthSquared);
		double x = rotation.X * inverseLength, y = rotation.Y * inverseLength;
		double z = rotation.Z * inverseLength, w = rotation.W * inverseLength;
		double r13 = 2 * (x * z + w * y);
		double r23 = 2 * (y * z - w * x);
		double r21 = 2 * (x * y + w * z);
		double r22 = 1 - 2 * (x * x + z * z);
		double r31 = 2 * (x * z - w * y);
		double r11 = 1 - 2 * (y * y + z * z);
		double angleX = Math.Asin(Math.Clamp(-r23, -1, 1));
		double cosineX = Math.Cos(angleX);
		double angleY = Math.Abs(cosineX) > 1e-5
			? Math.Atan2(r13, 1 - 2 * (x * x + y * y))
			: Math.Atan2(-r31, r11);
		double angleZ = Math.Abs(cosineX) > 1e-5 ? Math.Atan2(r21, r22) : 0;
		return new((float)PositiveDegrees(angleX), (float)PositiveDegrees(angleY), (float)PositiveDegrees(angleZ));
	}
	private static Quaternion FromEulerAngles(Vector3 degrees)
	{
		const double radians = Math.PI / 180.0;
		double halfX = degrees.X * radians * 0.5, halfY = degrees.Y * radians * 0.5,
			halfZ = degrees.Z * radians * 0.5;
		double sx = Math.Sin(halfX), cx = Math.Cos(halfX), sy = Math.Sin(halfY), cy = Math.Cos(halfY),
			sz = Math.Sin(halfZ), cz = Math.Cos(halfZ);
		return new((float)(cy * sx * cz + sy * cx * sz), (float)(sy * cx * cz - cy * sx * sz),
			(float)(cy * cx * sz - sy * sx * cz), (float)(cy * cx * cz + sy * sx * sz));
	}
	private static double PositiveDegrees(double radians)
	{
		double degrees = radians * (180.0 / Math.PI) % 360.0;
		return degrees < 0 ? degrees + 360.0 : degrees;
	}
	public static Color ColorField(string label, Color value, bool mixedValue = false, bool hdr = false)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.ColorField(label, value, mixedValue, hdr);
	}
	public static EditorReference? ObjectField(string label, EditorReference? value, EditorReferenceKind kind,
		string expectedType = "", bool mixedValue = false)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.ObjectField(label, value?.value, kind, expectedType, mixedValue) is { } edited ? new(edited) : null;
	}
	public static EditorReference? AssetField(string label, EditorReference? value,
		EditorReferenceKind kind = EditorReferenceKind.Asset, string expectedType = "", bool mixedValue = false)
	{
		_ = EditorApi.Generation;
		return NativeEditorBindings.AssetField(label, value?.value, kind, expectedType, mixedValue) is { } edited ? new(edited) : null;
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
				bool originalBoolean = property.BoolValue;
				bool editedBoolean = Toggle(caption, originalBoolean, property.HasMultipleDifferentValues);
				if (editedBoolean != originalBoolean) property.BoolValue = editedBoolean;
				break;
			case SerializedPropertyType.Integer:
				long originalInteger = property.LongValue;
				long editedInteger = IntField(caption, originalInteger, property.HasMultipleDifferentValues);
				if (editedInteger != originalInteger || property.HasMultipleDifferentValues && WasLastControlEdited())
					property.LongValue = editedInteger;
				break;
			case SerializedPropertyType.UnsignedInteger:
				ulong originalUnsigned = property.ULongValue;
				ulong editedUnsigned = ULongField(caption, originalUnsigned, property.HasMultipleDifferentValues);
				if (editedUnsigned != originalUnsigned || property.HasMultipleDifferentValues && WasLastControlEdited())
					property.ULongValue = editedUnsigned;
				break;
			case SerializedPropertyType.Float:
				double originalFloat = property.DoubleValue;
				double editedFloat = property.HasRange
					? SliderFloat(caption, originalFloat, property.Minimum, property.Maximum, property.HasMultipleDifferentValues)
					: FloatField(caption, originalFloat, property.HasMultipleDifferentValues);
				if (editedFloat != originalFloat || property.HasMultipleDifferentValues && WasLastControlEdited())
					property.DoubleValue = editedFloat;
				break;
			case SerializedPropertyType.String:
				string originalText = property.StringValue ?? string.Empty;
				string editedText = TextField(caption, originalText, property.HasMultipleDifferentValues);
				if (!StringComparer.Ordinal.Equals(editedText, originalText) ||
				    property.HasMultipleDifferentValues && WasLastControlEdited()) property.StringValue = editedText;
				break;
			case SerializedPropertyType.Enum:
				int originalIndex = property.HasMultipleDifferentValues ? -1 : property.EnumValueIndex;
				int editedIndex = Popup(caption, originalIndex, property.EnumNames, property.HasMultipleDifferentValues);
				if (editedIndex >= 0 && editedIndex != originalIndex) property.EnumValueIndex = editedIndex;
				break;
			case SerializedPropertyType.Vector2:
				Vector2 originalVector2 = property.Vector2Value;
				Vector2 editedVector2 = Vector2Field(caption, originalVector2, property.HasMultipleDifferentValues);
				if (!editedVector2.Equals(originalVector2) ||
				    property.HasMultipleDifferentValues && WasLastControlEdited()) property.Vector2Value = editedVector2;
				break;
			case SerializedPropertyType.Vector3:
				Vector3 originalVector3 = property.Vector3Value;
				Vector3 editedVector3 = Vector3Field(caption, originalVector3, property.HasMultipleDifferentValues);
				if (!editedVector3.Equals(originalVector3) ||
				    property.HasMultipleDifferentValues && WasLastControlEdited()) property.Vector3Value = editedVector3;
				break;
			case SerializedPropertyType.Vector4:
				Vector4 originalVector4 = property.Vector4Value;
				Vector4 editedVector4 = Vector4Field(caption, originalVector4, property.HasMultipleDifferentValues);
				if (!editedVector4.Equals(originalVector4) ||
				    property.HasMultipleDifferentValues && WasLastControlEdited()) property.Vector4Value = editedVector4;
				break;
			case SerializedPropertyType.Quaternion:
				Quaternion originalQuaternion = property.QuaternionValue;
				Quaternion editedQuaternion = QuaternionField(caption, originalQuaternion, property.HasMultipleDifferentValues);
				if (!editedQuaternion.Equals(originalQuaternion) ||
				    property.HasMultipleDifferentValues && WasLastControlEdited()) property.QuaternionValue = editedQuaternion;
				break;
			case SerializedPropertyType.Color:
				Color originalColor = property.ColorValue;
				Color editedColor = ColorField(caption, originalColor, property.HasMultipleDifferentValues, hdr: property.Hdr);
				if (!editedColor.Equals(originalColor) || property.HasMultipleDifferentValues && WasLastControlEdited())
					property.ColorValue = editedColor;
				break;
			case SerializedPropertyType.ObjectReference:
				EditorReference? originalObject = property.ObjectReferenceValue;
				EditorReference? editedObject = ObjectField(caption, originalObject, property.ReferenceKind, property.TypeName,
					property.HasMultipleDifferentValues);
				if (!SameReference(editedObject, originalObject) ||
				    property.HasMultipleDifferentValues && WasLastControlEdited()) property.ObjectReferenceValue = editedObject;
				break;
			case SerializedPropertyType.AssetReference:
				EditorReference? originalAsset = property.AssetReferenceValue;
				EditorReference? editedAsset = AssetField(caption, originalAsset, property.ReferenceKind, property.TypeName,
					property.HasMultipleDifferentValues);
				if (!SameReference(editedAsset, originalAsset) ||
				    property.HasMultipleDifferentValues && WasLastControlEdited()) property.AssetReferenceValue = editedAsset;
				break;
			case SerializedPropertyType.Array:
			case SerializedPropertyType.List:
			{
				int originalSize = property.ArraySize;
				string sizeLabel = property.HasMultipleDifferentArraySizes ? "—" : originalSize.ToString(
					System.Globalization.CultureInfo.InvariantCulture);
				string foldoutLabel = caption + " (" + sizeLabel + ")##" + property.PropertyPath;
				if (!FoldoutHeader(foldoutLabel)) break;
				try
				{
					long editedSize = IntField("Size##" + property.PropertyPath, originalSize,
						property.HasMultipleDifferentArraySizes);
					if (editedSize >= 0 && editedSize <= int.MaxValue &&
					    (editedSize != originalSize || property.HasMultipleDifferentArraySizes && WasLastControlEdited()))
						property.ArraySize = (int)editedSize;
					for (int index = 0; index < property.ArraySize; ++index)
					{
						string elementIndex = index.ToString(System.Globalization.CultureInfo.InvariantCulture);
						string elementId = property.PropertyPath + ".Array.data[" + elementIndex + "]";
						int draggedIndex = DragDropArrayElement(property.PropertyPath, index, property.ArraySize);
						if (draggedIndex >= 0)
						{
							property.MoveArrayElement(draggedIndex, index);
							return;
						}
						SameLine();
						SerializedProperty element = property.GetArrayElementAtIndex(index);
						PropertyField(element, "Element " + index + "##" + elementId + ".value");
						NewLine();
					}
				}
				finally { EndFoldout(); }
				break;
			}
			default:
				NativeEditorBindings.PropertyField(property.Revision, property.StoragePath, caption);
				break;
		}
	}
	public static bool DrawMaterialProperties(Material material, string? heading = null)
	{
		ArgumentNullException.ThrowIfNull(material);
		_ = EditorApi.Generation;
		bool changed = false;
		if (!string.IsNullOrEmpty(heading)) Text(heading);
		MaterialPropertyDescriptor[] properties = material.ShaderProperties;
		if (properties.Length == 0)
		{
			Color original = material.Color;
			Color edited = ColorField("Color", original, hdr: true);
			if (!edited.Equals(original)) { material.Color = edited; changed = true; }
			return changed;
		}
		foreach (MaterialPropertyDescriptor property in properties)
		{
			string id = property.Identifier;
			switch (property.Type)
			{
				case MaterialPropertyKind.Float:
				{
					float original = material.GetFloat(id);
					double edited = DragFloat(property.DisplayName + "##" + id, original, 0.01,
						-float.MaxValue, float.MaxValue);
					if (edited != original) { material.SetFloat(id, (float)edited); changed = true; }
					break;
				}
				case MaterialPropertyKind.Range:
				{
					float original = material.GetFloat(id);
					double edited = SliderFloat(property.DisplayName + "##" + id, original,
						property.RangeMinimum, property.RangeMaximum);
					if (edited != original) { material.SetFloat(id, (float)edited); changed = true; }
					break;
				}
				case MaterialPropertyKind.Int:
				{
					int original = material.GetInt(id);
					long edited = IntField(property.DisplayName + "##" + id, original);
					if (edited >= int.MinValue && edited <= int.MaxValue && edited != original)
					{
						material.SetInt(id, (int)edited);
						changed = true;
					}
					break;
				}
				case MaterialPropertyKind.Color:
				{
					Color original = material.GetColor(id);
					Color edited = ColorField(property.DisplayName + "##" + id, original, hdr: true);
					if (!edited.Equals(original)) { material.SetColor(id, edited); changed = true; }
					break;
				}
				case MaterialPropertyKind.Vector:
				{
					Vector4 original = material.GetVector(id);
					Vector4 edited = Vector4Field(property.DisplayName + "##" + id, original);
					if (!edited.Equals(original)) { material.SetVector(id, edited); changed = true; }
					break;
				}
				case MaterialPropertyKind.Texture2D:
				{
					TextureAsset? original = material.GetTexture(id);
					EditorReference? originalReference = original == null
						? null
						: EditorContext.ResolveAsset(original.Reference, EditorReferenceKind.Texture);
					EditorReference? edited = AssetField(property.DisplayName + "##" + id, originalReference,
						EditorReferenceKind.Texture);
					AssetReference? editedAsset = edited?.AssetReference;
					if (editedAsset?.Guid != original?.Reference.Guid ||
					    editedAsset?.LocalFileId != original?.Reference.LocalFileId)
					{
						material.SetTexture(id, editedAsset is { } identity ? TextureAsset.Load(identity) : null);
						changed = true;
					}
					break;
				}
			}
			SameLine();
			if (SmallButton("Reset##" + id))
			{
				material.RemovePropertyOverride(id);
				changed = true;
			}
		}
		return changed;
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
