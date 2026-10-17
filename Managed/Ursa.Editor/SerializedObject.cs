using System;

namespace Ursa.Editor;

public sealed class SerializedObject
{
	private readonly ulong generation = EditorApi.Generation;
	internal ulong Revision { get; private set; } = EditorApi.Revision;
	internal SerializedObject() { }
	internal void RequireValid() => EditorApi.RequireGeneration(generation);
	public EditorReference? TargetObject { get { RequireValid(); return EditorContext.Target; } }
	public bool HasModifiedProperties { get { RequireValid(); return NativeEditorBindings.HasModifiedProperties(Revision); } }
	public void Update() { RequireValid(); Revision = NativeEditorBindings.UpdateProperties(); }
	public SerializedProperty FindProperty(string path)
	{
		RequireValid();
		ArgumentException.ThrowIfNullOrEmpty(path);
		if (!path.StartsWith("/Fields/", StringComparison.Ordinal)) path = "/Fields/" + path.Replace("~", "~0").Replace("/", "~1");
		_ = NativeEditorBindings.GetProperty(Revision, path);
		return new(this, path);
	}
	public string[] GetPropertyPaths() { RequireValid(); return NativeEditorBindings.GetPropertyPaths(Revision); }
	public bool ApplyModifiedProperties() { RequireValid(); return NativeEditorBindings.ApplyProperties(Revision); }
	internal bool DrawDefaultInspector() { RequireValid(); return NativeEditorBindings.DrawDefaultInspector(Revision); }
}

public sealed class SerializedProperty
{
	private readonly SerializedObject owner;
	internal ulong Revision { get; }
	internal SerializedProperty(SerializedObject owner, string path) { this.owner = owner; Path = path; Revision = owner.Revision; }
	public string Path { get; }
	public SerializedObject SerializedObject => owner;
	internal void RequireValid()
	{
		owner.RequireValid();
		if (Revision != owner.Revision) throw new InvalidOperationException("SerializedProperty was invalidated by Update; find the property again.");
	}
	private SerializedPropertyState State { get { RequireValid(); return NativeEditorBindings.GetProperty(Revision, Path); } }
	private SerializedValue Read(SerializedPropertyType type)
	{
		var state = State;
		if (state.Type != type) throw new InvalidOperationException($"Property '{Path}' has type {state.Type}, expected {type}.");
		return state.Value;
	}
	private void Write(SerializedValue value) { RequireValid(); NativeEditorBindings.SetProperty(Revision, Path, value); }
	public SerializedPropertyType PropertyType => State.Type;
	public string TypeName => State.TypeName;
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
		set { RequireValid(); NativeEditorBindings.ResizeArray(Revision, Path, value); }
	}
	public SerializedProperty GetArrayElementAtIndex(int index)
	{
		if ((uint)index >= (uint)ArraySize) throw new ArgumentOutOfRangeException(nameof(index));
		return owner.FindProperty(Path + "/" + index.ToString(System.Globalization.CultureInfo.InvariantCulture));
	}
}
