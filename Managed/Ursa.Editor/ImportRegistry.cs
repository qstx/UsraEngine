using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace Ursa.Editor;

internal sealed partial class ImportRegistry : IImportExtensionSession
{
	private readonly Dictionary<string, (Type Type, ScriptedImporterInfo Info)> importers = new(StringComparer.Ordinal);
	private bool disposed;
	internal static IImportExtensionSession Create(Assembly[] assemblies, string[] reservedExtensions) =>
		CreateForTypes(assemblies.SelectMany(assembly => assembly.GetTypes()), reservedExtensions);

	internal static ImportRegistry CreateForTypes(IEnumerable<Type> types, string[] reservedExtensions)
	{
		var registry = new ImportRegistry();
		var extensions = new Dictionary<string, string>(StringComparer.Ordinal);
		foreach (string extension in reservedExtensions) extensions.Add(Normalize(extension), "native importer");
		try
		{
			foreach (Type type in types)
			{
				if (typeof(AssetPostprocessor).IsAssignableFrom(type) && !type.IsAbstract)
				{
					if (type.IsDefined(typeof(ScriptedImporterAttribute), inherit: false))
						throw new InvalidOperationException($"ScriptedImporter attribute requires a ScriptedImporter type: {type.FullName}.");
					registry.RegisterPostprocessor(type);
					continue;
				}
				ScriptedImporterAttribute? declaration = type.GetCustomAttribute<ScriptedImporterAttribute>(inherit: false);
				if (declaration == null)
				{
					if (!type.IsAbstract && typeof(ScriptedImporter).IsAssignableFrom(type))
						throw new InvalidOperationException($"ScriptedImporter '{type.FullName}' has no format declaration.");
					continue;
				}
				if (!typeof(ScriptedImporter).IsAssignableFrom(type) || !type.IsVisible || type.IsAbstract || type.ContainsGenericParameters || type.GetConstructor(Type.EmptyTypes) == null)
					throw new InvalidOperationException($"ScriptedImporter '{type.FullName}' must be public, concrete, non-generic and have a public parameterless constructor.");
				if (declaration.Version == 0 || declaration.Extensions.Length == 0)
					throw new InvalidOperationException($"ScriptedImporter '{type.FullName}' needs a nonzero version and at least one extension.");
				ScriptIdentity identity = BehaviourRegistry.IdentityFor(type);
				if (!Guid.TryParse(identity.SourceGuid, out Guid guid) || guid == Guid.Empty || identity.LocalFileId == 0)
					throw new InvalidOperationException($"ScriptedImporter '{type.FullName}' has no stable source asset identity.");
				string stableId = $"script:{guid:D}:{identity.LocalFileId}";
				var suffixes = declaration.Extensions.Select(Normalize).Order(StringComparer.Ordinal).ToArray();
				foreach (string extension in suffixes)
				{
					if (extensions.TryGetValue(extension, out string? previous))
						throw new InvalidOperationException($"Importer extension '{extension}' conflicts: {previous}; {type.FullName} ({stableId}).");
					extensions.Add(extension, $"{type.FullName} ({stableId})");
				}
				if (!registry.importers.TryAdd(stableId, (type, new()
				{
					SourceGuid = guid.ToString("D"), LocalFileId = identity.LocalFileId,
					ClrTypeName = type.FullName + ", " + type.Assembly.GetName().Name,
					Version = declaration.Version, Extensions = suffixes,
					DefaultSettings = ScriptFieldSchema.Serialize(Activator.CreateInstance(type)!)
				}))) throw new InvalidOperationException($"Duplicate importer type identity '{stableId}'.");
			}
			return registry;
		}
		catch { registry.Dispose(); throw; }
	}
	private static string Normalize(string extension)
	{
		if (string.IsNullOrEmpty(extension)) throw new InvalidOperationException("Importer extension is empty.");
		string normalized = (extension[0] == '.' ? extension : "." + extension).ToLowerInvariant();
		if (normalized.Length == 1 || normalized[1..].Any(value => !(value is >= 'a' and <= 'z' or >= '0' and <= '9' or '_' or '-')))
			throw new InvalidOperationException($"Invalid importer extension '{extension}'.");
		return normalized;
	}
	public ScriptedImporterInfo[] DescribeImporters()
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		return importers.OrderBy(entry => entry.Key, StringComparer.Ordinal).Select(entry =>
		{
			ScriptedImporterInfo value = entry.Value.Info;
			value.Extensions = (string[])value.Extensions.Clone();
			return value;
		}).ToArray();
	}
	public void ImportAsset(string identity)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		ImportExecution.RequireActive();
		if (NativeImportBindings.GetScope().Phase != 1) throw new InvalidOperationException("ScriptedImporter requires the scripted import phase.");
		if (!importers.TryGetValue(identity, out var registered)) throw new InvalidOperationException($"Required ScriptedImporter '{identity}' is missing.");
		var importer = (ScriptedImporter)Activator.CreateInstance(registered.Type)!;
		var context = new AssetImportContext();
		importer.BindImport(context);
		string settings = NativeImportBindings.GetImporterSettings();
		using (var payload = System.Text.Json.JsonDocument.Parse(settings))
			if (payload.RootElement.EnumerateObject().Any()) ScriptFieldSchema.Deserialize(importer, settings);
		importer.OnImportAsset(context);
	}
	public void Dispose() { disposed = true; EndInvocation(); importers.Clear(); postprocessors.Clear(); }
}
