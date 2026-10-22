using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Threading;
using Ursa.Bindings;

namespace Ursa;

[WireValue]
internal struct ImportReferenceInfo
{
	public ulong Token;
	public uint Kind;
	public ulong Instance;
	public NativeHandle Handle;
}

[WireValue]
internal struct ImportReferenceAssignment
{
	public ulong Token;
	public NativeAssetReference Asset;
}

internal static partial class ImportExecution
{
	[ThreadStatic] private static Dictionary<object, ulong>? referenceIds;
	[ThreadStatic] private static Dictionary<ulong, object>? referenceObjects;
	private static long nextReferenceEpoch;
	[ThreadStatic] private static ulong referenceEpoch;
	[ThreadStatic] private static Dictionary<ulong, (ScriptableObject Asset, string Payload)>? readOnlyAssets;
	internal static void RecordReadOnlyAssets(IEnumerable<ScriptableObject> assets)
	{
		if (!Active) return;
		readOnlyAssets ??= new();
		foreach (var asset in assets)
			if (!readOnlyAssets.ContainsKey(asset.InstanceId))
				readOnlyAssets.Add(asset.InstanceId, (asset, ScriptFieldSchema.Serialize(asset)));
	}
	private static void ValidateReadOnlyAssets()
	{
		if (readOnlyAssets == null) return;
		foreach (var asset in readOnlyAssets.Values)
			if (ScriptFieldSchema.Serialize(asset.Asset) != asset.Payload)
				throw new InvalidOperationException("A persistent ScriptableObject was modified during import; create a new candidate instead.");
	}
	internal static void RequireWritableAsset(ScriptableObject asset)
	{
		if (Active && readOnlyAssets?.ContainsKey(asset.InstanceId) == true)
			throw new InvalidOperationException("Persistent ScriptableObjects are read-only during import.");
	}
	private static void BeginReferences() => referenceEpoch = checked((ulong)Interlocked.Increment(ref nextReferenceEpoch));
	internal static void RequireActive()
	{
		ImportScopeValue scope = Current;
		NativeImportBindings.RequireInvocation(scope.Session, scope.Generation, scope.Invocation);
	}
	private static void ClearReferences() { referenceIds = null; referenceObjects = null; referenceEpoch = 0; readOnlyAssets = null; }
	internal static ImportReferenceInfo ReferenceFor(object value)
	{
		RequireActive();
		var result = new ImportReferenceInfo();
		switch (value)
		{
			case ScriptableObject asset:
				asset.RequireValid();
				if (readOnlyAssets?.ContainsKey(asset.InstanceId) == true)
					throw new InvalidOperationException("A persistent ScriptableObject cannot be registered as a new import output.");
				ScriptableObjectRegistry.RequireInstance(asset.InstanceId);
				result.Kind = 1; result.Instance = asset.InstanceId; break;
			case Mesh mesh: result.Kind = 2; result.Handle = mesh.Handle; break;
			case Material material: result.Kind = 3; result.Handle = material.Handle; break;
			case Texture2D texture: result.Kind = 4; result.Handle = texture.Handle; break;
			case GameObject gameObject: result.Kind = 5; result.Handle = gameObject.Handle; break;
			default: throw new InvalidOperationException("Object is not a publishable import candidate.");
		}
		if (result.Kind != 1) NativeImportBindings.RequireCandidate(result.Handle);
		referenceIds ??= new(ReferenceEqualityComparer.Instance);
		referenceObjects ??= new();
		if (!referenceIds.TryGetValue(value, out result.Token))
		{
			result.Token = checked((ulong)referenceIds.Count + 1);
			referenceIds.Add(value, result.Token);
			referenceObjects.Add(result.Token, value);
		}
		return result;
	}
	internal static ImportReferenceInfo[] DescribeReferences()
	{
		RequireActive();
		if (referenceObjects == null) return Array.Empty<ImportReferenceInfo>();
		var result = new List<ImportReferenceInfo>();
		foreach (object value in referenceObjects.Values) result.Add(ReferenceFor(value));
		return result.ToArray();
	}
	internal static void AssignReferences(ImportReferenceAssignment[] assignments)
	{
		RequireActive();
		var seen = new HashSet<ulong>();
		foreach (var assignment in assignments)
		{
			if (!seen.Add(assignment.Token) || referenceObjects == null || !referenceObjects.TryGetValue(assignment.Token, out object? value))
				throw new InvalidOperationException("Import output identity refers to an unknown or duplicate object token.");
			ReferenceFor(value);
			AssetReference identity = assignment.Asset.ToManaged();
			if (identity.LocalFileId == 0) throw new InvalidOperationException("Import output identity must have a nonzero local file ID.");
		}
		foreach (var assignment in assignments)
		{
			object value = referenceObjects![assignment.Token];
			AssetReference identity = assignment.Asset.ToManaged();
			if (value is ScriptableObject asset) asset.BindAsset(identity);
			else if (value is Texture2D texture) texture.BindImportReference(identity);
		}
	}
	internal static bool TrySerializeReference(object? value, out JsonElement binding)
	{
		binding = default;
		if (!Active || value == null) return false;
		if (value is ScriptableObject asset && asset.IsPersistent) return false;
		if (value is Texture texture && !string.IsNullOrEmpty(texture.Reference.Guid)) return false;
		if (value is not (ScriptableObject or Mesh or Material or Texture2D)) return false;
		if (value is Mesh or Material)
		{
			if (EngineApi.SnapshotResourceReference((RenderResource)value).GetProperty("kind").GetString() == "asset") return false;
			NativeImportBindings.RequireCandidate(((RenderResource)value).Handle);
		}
		ImportReferenceInfo reference = ReferenceFor(value);
		binding = JsonSerializer.SerializeToElement(new { kind = "import", invocation = Current.Invocation, epoch = referenceEpoch, token = reference.Token });
		return true;
	}
	internal static object ResolveReference(JsonElement binding, Type expectedType)
	{
		RequireActive();
		if (binding.GetProperty("invocation").GetUInt64() != Current.Invocation || binding.GetProperty("epoch").GetUInt64() != referenceEpoch || referenceObjects == null ||
			!referenceObjects.TryGetValue(binding.GetProperty("token").GetUInt64(), out object? value) || !expectedType.IsInstanceOfType(value))
			throw new InvalidOperationException("Import reference belongs to a closed or different invocation, or has the wrong type.");
		ReferenceFor(value);
		return value;
	}
}
