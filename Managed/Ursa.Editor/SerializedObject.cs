using System;
using System.Globalization;

namespace Ursa.Editor;

public sealed class SerializedObject
{
	private readonly ulong generation = EditorApi.Generation;
	private readonly object[] targetObjects;
	internal ulong Revision { get; private set; } = EditorApi.Revision;
	internal SerializedObject(object[] targets) { targetObjects = (object[])targets.Clone(); }
	internal void RequireValid() => EditorApi.RequireGeneration(generation);
	public EditorReference? TargetObject { get { RequireValid(); return EditorContext.Target; } }
	public object[] TargetObjects { get { RequireValid(); return (object[])targetObjects.Clone(); } }
	public bool IsEditingMultipleObjects { get { RequireValid(); return targetObjects.Length > 1; } }
	public bool HasModifiedProperties { get { RequireValid(); return NativeEditorBindings.HasModifiedProperties(Revision); } }
	public void Update() { RequireValid(); Revision = NativeEditorBindings.UpdateProperties(); }
	public SerializedProperty FindProperty(string propertyPath)
	{
		RequireValid();
		ArgumentException.ThrowIfNullOrEmpty(propertyPath);
		string[] segments = propertyPath.Split('.');
		foreach (string segment in segments)
			if (segment.Length == 0 || segment.Contains('/') || segment.Contains('~'))
				throw new ArgumentException("Use field names, dotted child paths and Array.data[index] paths.", nameof(propertyPath));
		if (segments[0].Contains('[') || segments[0].Contains(']'))
			throw new ArgumentException("The property path must begin with a field name.", nameof(propertyPath));
		string storagePath = "/Fields/" + segments[0];
		string resolvedPath = segments[0];
		SerializedPropertyState state = NativeEditorBindings.GetProperty(Revision, storagePath);
		for (int index = 1; index < segments.Length; index++)
		{
			string segment = segments[index];
			if (state.Type is SerializedPropertyType.Array or SerializedPropertyType.List)
			{
				if (segment != "Array" || ++index >= segments.Length ||
					!segments[index].StartsWith("data[", StringComparison.Ordinal) || !segments[index].EndsWith(']') ||
					!int.TryParse(segments[index].AsSpan(5, segments[index].Length - 6), NumberStyles.None, CultureInfo.InvariantCulture, out int element))
					throw new ArgumentException("Array elements use Array.data[index].", nameof(propertyPath));
				storagePath += "/" + element.ToString(CultureInfo.InvariantCulture);
				resolvedPath += ".Array.data[" + element.ToString(CultureInfo.InvariantCulture) + "]";
			}
			else
			{
				int component = ComponentIndex(state.Type, segment);
				if (component < 0)
					throw new ArgumentException($"Property '{resolvedPath}' has no supported child '{segment}'.", nameof(propertyPath));
				storagePath += "/" + component.ToString(CultureInfo.InvariantCulture);
				resolvedPath += "." + segment;
			}
			state = NativeEditorBindings.GetProperty(Revision, storagePath);
		}
		return new(this, resolvedPath, storagePath);
	}
	private static int ComponentIndex(SerializedPropertyType type, string name)
	{
		string components = type switch
		{
			SerializedPropertyType.Vector2 => "xy",
			SerializedPropertyType.Vector3 => "xyz",
			SerializedPropertyType.Vector4 or SerializedPropertyType.Quaternion => "xyzw",
			SerializedPropertyType.Color => "rgba",
			_ => ""
		};
		return name.Length == 1 ? components.IndexOf(name[0]) : -1;
	}
	public string[] GetPropertyPaths()
	{
		RequireValid();
		string[] paths = NativeEditorBindings.GetPropertyPaths(Revision);
		for (int index = 0; index < paths.Length; index++)
			paths[index] = paths[index]["/Fields/".Length..].Replace("~1", "/").Replace("~0", "~");
		return paths;
	}
	public bool ApplyModifiedProperties() { RequireValid(); return NativeEditorBindings.ApplyProperties(Revision); }
	internal bool DrawDefaultInspector() { RequireValid(); return NativeEditorBindings.DrawDefaultInspector(Revision); }
	internal bool DrawDefaultInspectorManaged()
	{
		RequireValid();
		foreach (string path in GetPropertyPaths())
		{
			SerializedProperty property = FindProperty(path);
			if (!property.Hidden)
				EditorGui.PropertyField(property, property.DisplayName);
		}
		return ApplyModifiedProperties();
	}
}

public sealed class SerializedProperty
{
	private readonly SerializedObject owner;
	internal ulong Revision { get; }
	internal SerializedProperty(SerializedObject owner, string propertyPath, string storagePath)
	{
		this.owner = owner;
		PropertyPath = propertyPath;
		StoragePath = storagePath;
		Revision = owner.Revision;
	}
	public string PropertyPath { get; }
	internal string StoragePath { get; }
	public SerializedObject SerializedObject => owner;
	internal void RequireValid()
	{
		owner.RequireValid();
		if (Revision != owner.Revision) throw new InvalidOperationException("SerializedProperty was invalidated by Update; find the property again.");
	}
	private SerializedPropertyState State { get { RequireValid(); return NativeEditorBindings.GetProperty(Revision, StoragePath); } }
	private SerializedValue Read(SerializedPropertyType type)
	{
		var state = State;
		if (state.Type != type) throw new InvalidOperationException($"Property '{PropertyPath}' has type {state.Type}, expected {type}.");
		return state.Value;
	}
	private void Write(SerializedValue value) { RequireValid(); NativeEditorBindings.SetProperty(Revision, StoragePath, value); }
	public SerializedPropertyType PropertyType => State.Type;
	public bool HasMultipleDifferentValues => State.HasMultipleDifferentValues;
	public bool HasMultipleDifferentArraySizes => State.HasMultipleDifferentArraySizes;
	public string TypeName => State.TypeName;
	public string DisplayName => State.DisplayName;
	public bool Hidden => State.Hidden;
	public bool HasRange => State.HasRange;
	public double Minimum => State.Minimum;
	public double Maximum => State.Maximum;
	public bool Hdr => State.Hdr;
	public EditorReferenceKind ReferenceKind => State.ReferenceKind;
	public bool IsNull
	{
		get => State.Value.IsNull;
		set
		{
			if (!value) throw new InvalidOperationException("Assign a typed value or resize the collection to clear its null state.");
			var current = State.Value; current.IsNull = true; Write(current);
		}
	}
	public bool BoolValue { get => Read(SerializedPropertyType.Boolean).Boolean; set => Write(new() { Type = SerializedPropertyType.Boolean, Boolean = value }); }
	public int IntValue { get => checked((int)LongValue); set => LongValue = value; }
	public long LongValue { get => Read(SerializedPropertyType.Integer).Integer; set => Write(new() { Type = SerializedPropertyType.Integer, Integer = value }); }
	public uint UIntValue { get => checked((uint)ULongValue); set => ULongValue = value; }
	public ulong ULongValue { get => Read(SerializedPropertyType.UnsignedInteger).UnsignedInteger; set => Write(new() { Type = SerializedPropertyType.UnsignedInteger, UnsignedInteger = value }); }
	public float FloatValue { get => checked((float)DoubleValue); set => DoubleValue = value; }
	public double DoubleValue { get => Read(SerializedPropertyType.Float).Float; set => Write(new() { Type = SerializedPropertyType.Float, Float = value }); }
	public string? StringValue { get { var value = Read(SerializedPropertyType.String); return value.IsNull ? null : value.String; } set => Write(new() { Type = SerializedPropertyType.String, String = value, IsNull = value == null }); }
	public Vector2 Vector2Value { get { var v = Read(SerializedPropertyType.Vector2).Vector; return new(v.X, v.Y); } set => Write(new() { Type = SerializedPropertyType.Vector2, Vector = new(value.X, value.Y, 0, 0) }); }
	public Vector3 Vector3Value { get { var v = Read(SerializedPropertyType.Vector3).Vector; return new(v.X, v.Y, v.Z); } set => Write(new() { Type = SerializedPropertyType.Vector3, Vector = new(value.X, value.Y, value.Z, 0) }); }
	public Vector4 Vector4Value { get => Read(SerializedPropertyType.Vector4).Vector; set => Write(new() { Type = SerializedPropertyType.Vector4, Vector = value }); }
	public Quaternion QuaternionValue { get { var v = Read(SerializedPropertyType.Quaternion).Vector; return new(v.X, v.Y, v.Z, v.W); } set => Write(new() { Type = SerializedPropertyType.Quaternion, Vector = new(value.X, value.Y, value.Z, value.W) }); }
	public Color ColorValue { get { var v = Read(SerializedPropertyType.Color).Vector; return new(v.X, v.Y, v.Z, v.W); } set => Write(new() { Type = SerializedPropertyType.Color, Vector = new(value.R, value.G, value.B, value.A) }); }
	public bool EnumIsUnsigned => Read(SerializedPropertyType.Enum).EnumUnsigned;
	public long EnumLongValue { get { var value = Read(SerializedPropertyType.Enum); if (value.EnumUnsigned) throw new InvalidOperationException("Enum uses unsigned storage."); return value.Integer; } set { var current = Read(SerializedPropertyType.Enum); if (current.EnumUnsigned) throw new InvalidOperationException("Enum uses unsigned storage."); current.Integer = value; Write(current); } }
	public ulong EnumULongValue { get { var value = Read(SerializedPropertyType.Enum); if (!value.EnumUnsigned) throw new InvalidOperationException("Enum uses signed storage."); return value.UnsignedInteger; } set { var current = Read(SerializedPropertyType.Enum); if (!current.EnumUnsigned) throw new InvalidOperationException("Enum uses signed storage."); current.UnsignedInteger = value; Write(current); } }
	public string[] EnumNames { get { _ = Read(SerializedPropertyType.Enum); return State.EnumNames; } }
	public int EnumValueIndex
	{
		get { var state = State; _ = Read(SerializedPropertyType.Enum); return state.Value.EnumUnsigned ? Array.IndexOf(state.EnumUnsignedValues, state.Value.UnsignedInteger) : Array.IndexOf(state.EnumSignedValues, state.Value.Integer); }
		set
		{
			var state = State; _ = Read(SerializedPropertyType.Enum);
			if ((uint)value >= (uint)state.EnumNames.Length) throw new ArgumentOutOfRangeException(nameof(value));
			if (state.Value.EnumUnsigned) EnumULongValue = state.EnumUnsignedValues[value]; else EnumLongValue = state.EnumSignedValues[value];
		}
	}
	public EditorReference? ObjectReferenceValue
	{
		get { var value = Read(SerializedPropertyType.ObjectReference); return value.Reference is { } reference ? new(reference) : null; }
		set => Write(new() { Type = SerializedPropertyType.ObjectReference, IsNull = value == null, Reference = value?.value });
	}
	public EditorReference? AssetReferenceValue
	{
		get { var value = Read(SerializedPropertyType.AssetReference); return value.Reference is { } reference ? new(reference) : null; }
		set => Write(new() { Type = SerializedPropertyType.AssetReference, IsNull = value == null, Reference = value?.value });
	}
	public bool IsArray => PropertyType is SerializedPropertyType.Array or SerializedPropertyType.List;
	public SerializedPropertyType ElementType => State.ElementType;
	public int ArraySize
	{
		get { var state = State; if (state.Type is not (SerializedPropertyType.Array or SerializedPropertyType.List)) throw new InvalidOperationException("Property is not a collection."); return state.Value.ArraySize; }
		set { RequireValid(); NativeEditorBindings.ResizeArray(Revision, StoragePath, value); }
	}
	public SerializedProperty GetArrayElementAtIndex(int index)
	{
		if ((uint)index >= (uint)ArraySize) throw new ArgumentOutOfRangeException(nameof(index));
		return owner.FindProperty(PropertyPath + ".Array.data[" + index.ToString(CultureInfo.InvariantCulture) + "]");
	}
	public void MoveArrayElement(int fromIndex, int toIndex)
	{
		RequireValid();
		if ((uint)fromIndex >= (uint)ArraySize || (uint)toIndex >= (uint)ArraySize)
			throw new ArgumentOutOfRangeException(nameof(fromIndex));
		if (fromIndex != toIndex)
			NativeEditorBindings.MoveArrayElement(Revision, StoragePath, fromIndex, toIndex);
	}
	public SerializedProperty FindPropertyRelative(string relativePropertyPath)
	{
		RequireValid();
		ArgumentException.ThrowIfNullOrEmpty(relativePropertyPath);
		return owner.FindProperty(PropertyPath + "." + relativePropertyPath);
	}
}
