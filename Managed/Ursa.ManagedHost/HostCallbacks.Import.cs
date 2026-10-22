using System;
using Ursa.Bindings;

namespace Ursa.ManagedHost;

internal static class ImportCallbacks
{
	internal static ulong CreateScriptableObject(ulong session, string typeName)
	{
		ScriptSessions.RequireImport(session);
		if (typeName.Length == 0 || typeName.Length > 4096) throw new ArgumentException("Invalid import ScriptableObject type name.");
		Type type = BehaviourRegistry.ResolveType(typeName) ?? throw new InvalidOperationException($"Import asset type is missing: {typeName}");
		ScriptableObject asset = ScriptableObject.CreateInstance(type);
		asset.ValidateFields();
		ImportExecution.ReferenceFor(asset);
		return asset.InstanceId;
	}
	internal static ImportObjectArchive SerializeScriptableObject(ulong session, ulong instance)
	{
		ScriptSessions.RequireImport(session);
		ScriptableObject asset = ScriptableObjectRegistry.RequireInstance(instance);
		ScriptSessions.RequirePublishableImportType(session, asset.GetType());
		return new() { Version = 1, Json = ScriptFieldSchema.Serialize(asset) };
	}
	internal static void DeserializeScriptableObject(ulong session, ulong instance, ImportObjectArchive archive)
	{
		ScriptSessions.RequireImport(session);
		if (archive.Version != 1 || archive.Json.Length == 0 || archive.Json.Length > 64 * 1024 * 1024)
			throw new ArgumentException("Invalid import candidate field archive.");
		ScriptFieldSchema.Deserialize(ScriptableObjectRegistry.RequireInstance(instance), archive.Json);
		ScriptFieldSchema.ResolvePendingReferences(ScriptableObjectRegistry.RequireInstance(instance));
	}
	internal static ImportReferenceInfo[] GetReferences(ulong session)
	{
		ScriptSessions.RequireImport(session);
		return ImportExecution.DescribeReferences();
	}
	internal static void AssignReferences(ulong session, ImportReferenceAssignment[] assignments)
	{
		ScriptSessions.RequireImport(session);
		ImportExecution.AssignReferences(assignments);
	}
}
