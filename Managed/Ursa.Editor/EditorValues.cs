using Ursa.Bindings;

namespace Ursa.Editor;

[WireValue]
public enum EditorReferenceKind : uint
{
	None, GameObject, Component, Asset, Mesh, Material, Texture, ScriptableObject, Shader, ComputeShader
}

[WireValue]
public enum EditorDocumentKind : uint { Unknown, Scene, Prefab, Texture, Material, Shader, Mesh }

[WireValue]
public enum SerializedPropertyType : uint
{
	Unsupported, Boolean, Integer, UnsignedInteger, Float, String, Enum,
	Vector2, Vector3, Vector4, Quaternion, Color, ObjectReference, AssetReference, Array, List
}

public sealed class EditorReference
{
	internal readonly EditorReferenceValue value;
	internal EditorReference(EditorReferenceValue value) => this.value = value;
	public EditorReferenceKind Kind => value.Kind;
	public string PersistentId => value.Id;
	public string TypeId => value.TypeId;
	public AssetReference? AssetReference => value.Asset?.ToManaged();
}

public sealed class EditorSelection
{
	internal EditorSelection(EditorSelectionValue value)
	{
		Reference = new(value.Reference);
		Name = value.Name;
	}
	public EditorReference Reference { get; }
	public string Name { get; }
}

public sealed class EditorDocument
{
	internal EditorDocument(EditorDocumentValue value) { Id = value.Id; Path = value.Path; Kind = value.Kind; Dirty = value.Dirty; }
	public string Id { get; }
	public string Path { get; }
	public EditorDocumentKind Kind { get; }
	public bool Dirty { get; }
}

public sealed class EditorAsset
{
	internal EditorAsset(EditorAssetValue value) { Reference = new(value.Reference); Path = value.Path; Name = value.Name; IsSubAsset = value.IsSubAsset; }
	public EditorReference Reference { get; }
	public string Path { get; }
	public string Name { get; }
	public bool IsSubAsset { get; }
}

[WireValue]
internal struct EditorScopeValue
{
	public ulong Generation;
	public ulong Session;
	public ulong Revision;
	public bool HasTarget;
}

[WireValue]
internal struct EditorImporterInfo
{
	public string Path;
	public string Identity;
	public string ClrTypeName;
	public NativeAssetReference Reference;
}

[WireValue]
internal struct EditorReferenceValue
{
	public EditorReferenceKind Kind;
	public ulong Scope;
	public string Id;
	public NativeAssetReference? Asset;
	public string TypeId;
}

[WireValue]
internal struct EditorSelectionValue
{
	public EditorReferenceValue Reference;
	public string Name;
}

[WireValue]
internal struct EditorDocumentValue
{
	public string Id;
	public string Path;
	public EditorDocumentKind Kind;
	public bool Dirty;
}

[WireValue]
internal struct EditorAssetValue
{
	public EditorReferenceValue Reference;
	public string Path;
	public string Name;
	public bool IsSubAsset;
}

[WireValue]
internal struct SerializedValue
{
	public SerializedPropertyType Type;
	public bool IsNull;
	public bool Boolean;
	public long Integer;
	public ulong UnsignedInteger;
	public double Float;
	public string? String;
	public Vector4 Vector;
	public EditorReferenceValue? Reference;
	public int ArraySize;
	public bool EnumUnsigned;
}

[WireValue]
internal struct SerializedPropertyState
{
	public SerializedPropertyType Type;
	public SerializedPropertyType ElementType;
	public bool HasMultipleDifferentValues;
	public bool HasMultipleDifferentArraySizes;
	public EditorReferenceKind ReferenceKind;
	public string TypeName;
	public string DisplayName;
	public bool Hidden;
	public bool HasRange;
	public double Minimum;
	public double Maximum;
	public bool Hdr;
	public string[] EnumNames;
	public long[] EnumSignedValues;
	public ulong[] EnumUnsignedValues;
	public SerializedValue Value;
}
