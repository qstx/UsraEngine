using Ursa.Bindings;

namespace Ursa;

[BindingModule("Runtime", Required = true)]
[NativeHeader("Scripting/Bindings/SerializationServices.hpp")]
internal static partial class NativeSerializationBindings
{
	[NativeMethod("Ursa::Scripting::Bindings::SerializationServices::SnapshotMesh")]
	internal static partial ResourceReferenceArchive SnapshotMesh([HandleKind(4)] NativeHandle mesh);
	[NativeMethod("Ursa::Scripting::Bindings::SerializationServices::SnapshotMaterial")]
	internal static partial ResourceReferenceArchive SnapshotMaterial([HandleKind(5)] NativeHandle material);
	[NativeMethod("Ursa::Scripting::Bindings::SerializationServices::RestoreMesh")]
	internal static partial NativeHandle RestoreMesh(ResourceReferenceArchive archive);
	[NativeMethod("Ursa::Scripting::Bindings::SerializationServices::RestoreMaterial")]
	internal static partial NativeHandle RestoreMaterial(ResourceReferenceArchive archive);
	[NativeMethod("Ursa::Scripting::Bindings::SerializationServices::LoadScriptableAssetGraph")]
	internal static partial ScriptableAssetGraphArchive LoadScriptableAssetGraph(NativeAssetReference root);
}

[SerializedArchive]
internal struct ResourceReferenceArchive
{
	public uint Version;
	public string Json;
}

[SerializedArchive]
internal struct ScriptableAssetGraphArchive
{
	public uint Version;
	public string Json;
}
