using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Microsoft.CodeAnalysis;

internal sealed class ScriptIdentityTable
{
	public uint Version { get; set; } = 1;
	public List<StoredScriptIdentity> Types { get; set; } = new();
}

internal sealed class StoredScriptIdentity
{
	public string TypeName { get; set; } = string.Empty;
	public string Id { get; set; } = string.Empty;
	public uint Version { get; set; } = 1;
	public ulong LocalFileId { get; set; }
	public bool Primary { get; set; }
	public bool Legacy { get; set; }
}

internal sealed class ScriptIdentityMetadata
{
	private readonly HashSet<StoredScriptIdentity> assigned = new();
	private readonly HashSet<string> ids = new(StringComparer.Ordinal);
	private readonly Dictionary<string, string> origins = new(StringComparer.Ordinal);

	internal ScriptIdentityMetadata(IEnumerable<SourceEntry> sources)
	{
		foreach (SourceEntry source in sources)
		{
			ScriptIdentityTable table = source.Identities;
			if (table == null || table.Version != 1 || table.Types == null || table.Types.Count > 4096)
				throw new InvalidDataException($"Invalid script metadata schema in {source.VirtualPath}.");
			var names = new HashSet<string>(StringComparer.Ordinal);
			var localIds = new HashSet<ulong>();
			int primaryCount = 0;
			foreach (StoredScriptIdentity entry in table.Types)
			{
				if (string.IsNullOrWhiteSpace(entry.TypeName) || string.IsNullOrWhiteSpace(entry.Id) ||
				    entry.Version == 0 || entry.LocalFileId == 0 || !names.Add(entry.TypeName) ||
				    !localIds.Add(entry.LocalFileId) || !ids.Add(entry.Id) || (entry.Primary && ++primaryCount > 1))
					throw new InvalidDataException($"Duplicate or invalid script metadata identity in {source.VirtualPath}.");
				origins[entry.Id] = $"{entry.TypeName} in {source.VirtualPath}";
			}
		}
	}

	internal StoredScriptIdentity Resolve(SourceEntry source, INamedTypeSymbol type,
		IReadOnlyList<INamedTypeSymbol> candidates, string? declaredId, uint declaredVersion,
		Func<string, ulong> makeLocalFileId)
	{
		string typeName = type.ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat);
		string fileName = Path.GetFileNameWithoutExtension(source.Path);
		bool primary = type.Name == fileName || candidates.Count == 1;
		List<StoredScriptIdentity> entries = source.Identities.Types;
		StoredScriptIdentity? identity = entries.SingleOrDefault(entry => entry.TypeName == typeName);
		if (identity == null && declaredId != null)
			identity = entries.SingleOrDefault(entry => entry.Id == declaredId);
		if (identity == null && primary)
			identity = entries.SingleOrDefault(entry => entry.Primary);
		if (identity == null)
		{
			StoredScriptIdentity[] sameSimpleName = entries.Where(entry =>
				entry.TypeName[(entry.TypeName.LastIndexOf('.') + 1)..] == type.Name).ToArray();
			if (sameSimpleName.Length > 1)
				throw new InvalidDataException($"Ambiguous script rename in {source.VirtualPath}; metadata matches multiple types named {type.Name}.");
			if (sameSimpleName.Length == 1) identity = sameSimpleName[0];
		}
		if (identity != null)
		{
			if (!assigned.Add(identity))
				throw new InvalidDataException($"Ambiguous script metadata mapping for {typeName} in {source.VirtualPath}.");
			if (declaredId != null && declaredId != identity.Id)
				throw new InvalidDataException($"Legacy UrsaScriptType differs from .meta identity for {typeName}; preserve the metadata ID.");
			identity.TypeName = typeName;
			if (declaredId != null)
			{
				identity.Version = declaredVersion;
				identity.Legacy = true;
			}
		}
		else
		{
			// Preserve the old automatic identity on first migration; subsequent lookups use .meta.
			string id = declaredId ?? $"auto:{source.Guid}:{typeName}";
			if (!ids.Add(id))
				throw new InvalidDataException($"Duplicate ScriptType '{id}': {origins[id]}; {typeName} in {source.VirtualPath}.");
			origins[id] = $"{typeName} in {source.VirtualPath}";
			identity = new StoredScriptIdentity
			{
				TypeName = typeName, Id = id, Version = declaredVersion, Legacy = declaredId != null,
				LocalFileId = makeLocalFileId(id)
			};
			entries.Add(identity);
			assigned.Add(identity);
		}
		if (primary)
		{
			foreach (StoredScriptIdentity entry in entries) entry.Primary = false;
			identity.Primary = true;
		}
		return identity;
	}
}
