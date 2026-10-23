using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Threading;
using Ursa;
using Ursa.Bindings;

namespace Ursa.ManagedHost;

// Stable bridge entries survive; project contexts and registrations are session owned.
internal static class ScriptSessions
{
	private sealed class ProjectContext : AssemblyLoadContext
	{
		internal readonly Dictionary<string, byte[]> images = new(StringComparer.OrdinalIgnoreCase);
		internal readonly Dictionary<string, byte[]> symbols = new(StringComparer.OrdinalIgnoreCase);
		internal readonly Dictionary<string, Assembly> assemblies = new(StringComparer.OrdinalIgnoreCase);
		internal readonly bool editor;

		internal ProjectContext(ulong id, bool editor) : base($"Ursa.Project.{id}", isCollectible: true)
		{
			this.editor = editor;
		}

		protected override Assembly? Load(AssemblyName name)
		{
			string identity = name.Name ?? throw new InvalidOperationException("Assembly identity is empty.");
			if (identity == typeof(Behaviour).Assembly.GetName().Name) return typeof(Behaviour).Assembly;
			if (identity == typeof(ScriptSessions).Assembly.GetName().Name) return typeof(ScriptSessions).Assembly;
			if (identity == typeof(Ursa.Bindings.BindingModuleAttribute).Assembly.GetName().Name)
				return typeof(Ursa.Bindings.BindingModuleAttribute).Assembly;
			if (identity == typeof(Ursa.Rendering.RenderContextToken).Assembly.GetName().Name)
				return typeof(Ursa.Rendering.RenderContextToken).Assembly;
			if (identity == "Ursa.Editor")
			{
				if (!editor) throw new InvalidOperationException("Player sessions cannot load Ursa.Editor.");
				return sharedEditorAssembly ?? throw new InvalidOperationException("Shared Ursa.Editor API has not been supplied by the host.");
			}
			if (assemblies.TryGetValue(identity, out Assembly? loaded)) return loaded;
			if (images.TryGetValue(identity, out byte[]? bytes))
			{
				using var stream = new MemoryStream(bytes, writable: false);
				using var pdb = symbols.TryGetValue(identity, out byte[]? symbolBytes) ? new MemoryStream(symbolBytes, writable: false) : null;
				Assembly result = pdb == null ? LoadFromStream(stream) : LoadFromStream(stream, pdb);
				assemblies.Add(identity, result);
				return result;
			}
			// Only framework assemblies may fall back to CLR; project dependencies come from VFS.
			if (frameworkNames.Contains(identity)) return null;
			throw new FileNotFoundException($"Missing project dependency '{identity}' in the session content manifest.");
		}

		internal Type? ResolveType(string typeName)
		{
			int comma = typeName.IndexOf(',');
			string fullName = comma < 0 ? typeName : typeName[..comma].Trim();
			string? assemblyName = comma < 0 ? null : typeName[(comma + 1)..].Split(',')[0].Trim();
			if (assemblyName != null && images.ContainsKey(assemblyName)) LoadFromAssemblyName(new AssemblyName(assemblyName));
			foreach (Assembly assembly in assemblies.Values.ToArray())
			{
				if (assemblyName != null && assembly.GetName().Name != assemblyName) continue;
				Type? result = assembly.GetType(fullName, throwOnError: false);
				if (result != null) return result;
				result = assembly.GetTypes().FirstOrDefault(type => type.FullName?.Replace('+', '.') == fullName || type.Name == fullName);
				if (result != null) return result;
			}
			Assembly?[] sharedAssemblies =
			{
				typeof(Behaviour).Assembly,
				typeof(ScriptSessions).Assembly,
				typeof(Ursa.Bindings.BindingModuleAttribute).Assembly,
				typeof(Ursa.Rendering.RenderContextToken).Assembly,
				sharedEditorAssembly
			};
			foreach (Assembly? assembly in sharedAssemblies)
			{
				if (assembly == null || (assemblyName != null && assembly.GetName().Name != assemblyName)) continue;
				Type? result = assembly.GetType(fullName, throwOnError: false);
				if (result != null) return result;
				result = assembly.GetTypes().FirstOrDefault(type => type.FullName?.Replace('+', '.') == fullName || type.Name == fullName);
				if (result != null) return result;
			}
			return null;
		}
	}

	private sealed class Session
	{
		internal readonly ProjectContext context;
		internal BindingSnapshot bindings;
		internal IEditorExtensionSession? extensions;
		internal IImportExtensionSession? importExtensions;
		internal bool importing;
		internal string preparedReload = string.Empty;
		internal readonly Dictionary<string, ScriptIdentity> identities = new(StringComparer.Ordinal);
		internal readonly HashSet<Type> runtimeTypes = new();

		internal Session(ulong id, bool editor)
		{
			context = new ProjectContext(id, editor);
			bindings = BindingRegistry.Fork(id);
		}
	}

	private static readonly HashSet<string> frameworkNames = new(
		((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") ?? string.Empty)
		.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
		.Select(path => Path.GetFileNameWithoutExtension(path)).Where(name => name.StartsWith("System.", StringComparison.Ordinal) ||
			name.StartsWith("Microsoft.", StringComparison.Ordinal) || name is "System" or "mscorlib" or "netstandard"), StringComparer.OrdinalIgnoreCase);
	[ThreadStatic] private static Dictionary<ulong, Session>? ownerSessions;
	private static Dictionary<ulong, Session> sessions => ownerSessions ??= new();
	private static long nextId;
	[ThreadStatic] private static int ownerThread;
	private static volatile bool faulted;
	[ThreadStatic] private static ProjectContext? legacyContext;
	internal static readonly object editorAssemblyGate = new();
	internal static volatile Assembly? sharedEditorAssembly;
	internal static string sharedEditorFingerprint = string.Empty;

	internal static void RequireThread()
	{
		ScriptableObjectRegistry.RequireThread();
		if (ownerThread == 0) ownerThread = Environment.CurrentManagedThreadId;
		if (ownerThread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Script sessions require the host main thread.");
	}

	internal static void BeginImportInvocation(ulong session, ulong generation, ulong invocation, ImportBindingCapability capability)
	{
		RequireThread();
		if (ImportExecution.Active) throw new InvalidOperationException("Synchronous import reentry is forbidden.");
		Select(session);
		if (!sessions[session].importing) throw new InvalidOperationException("Import invocations require an independent Import script session.");
		ImportExecution.Begin(session, generation, invocation, capability);
	}

	internal static void EndImportInvocation(ulong session, ulong generation, ulong invocation)
	{
		RequireThread();
		try { ImportExecution.End(session, generation, invocation); }
		finally { if (sessions.TryGetValue(session, out var owned)) owned.importExtensions?.EndInvocation(); }
	}

	internal static void RequireImport(ulong session)
	{
		RequireThread();
		ImportExecution.RequireActive();
		if (ImportExecution.Current.Session != session || !sessions.TryGetValue(session, out var owned) || !owned.importing)
			throw new InvalidOperationException("Callback requires its independent Import script session.");
	}

	internal static ulong Create(bool editor)
	{
		RequireThread();
		if (ImportExecution.Active) throw new InvalidOperationException("Import callbacks cannot create script sessions.");
		Ursa.Rendering.RenderPipelineManager.RequireSessionBoundary();
		if (faulted) throw new InvalidOperationException("A project context did not unload. Restart before creating another script session.");
		ulong id = checked((ulong)Interlocked.Increment(ref nextId));
		ulong previous = BehaviourRegistry.currentSession;
		var candidate = new Session(id, editor);
		try
		{
			sessions.Add(id, candidate);
			Select(id);
		}
		catch
		{
			sessions.Remove(id);
			BindingRegistry.Invalidate(candidate.bindings);
			candidate.context.Unload();
			if (previous != 0) Select(previous);
			throw;
		}
		return id;
	}

	internal static ulong CreateImport()
	{
		ulong id = Create(true);
		sessions[id].importing = true;
		return id;
	}

	internal static void Select(ulong id)
	{
		RequireThread();
		if (ImportExecution.Active)
		{
			if (ImportExecution.Current.Session != id) throw new InvalidOperationException("Import callbacks cannot switch script sessions.");
			BindingRegistry.RequireCurrent();
			return;
		}
		if (!sessions.TryGetValue(id, out Session? session)) throw new InvalidOperationException("Script session is closed or unknown.");
		BindingRegistry.Restore(session.bindings);
		Ursa.Rendering.RenderPipelineManager.SelectSession(id);
		BehaviourRegistry.currentSession = id;
		ScriptableObjectRegistry.SelectSession(id);
		ScriptableObjectRegistry.graphLoader = LoadScriptableAssetGraph;
		BehaviourRegistry.typeResolver = session.context.ResolveType;
		BehaviourRegistry.identityResolver = type => session.identities.TryGetValue(type.FullName ?? type.Name, out ScriptIdentity identity)
			? identity : throw new InvalidOperationException($"Script type is missing from the active catalog: {type.FullName}");
		BehaviourRegistry.assemblyResolver = () => session.context.assemblies.Values
			.Append(typeof(Behaviour).Assembly)
			.Append(typeof(Ursa.Rendering.RenderContextToken).Assembly)
			.Append(typeof(Ursa.Bindings.BindingModuleAttribute).Assembly)
			.Concat(sharedEditorAssembly == null ? Array.Empty<Assembly>() : new[] { sharedEditorAssembly })
			.Distinct();
	}

	private static IReadOnlyList<ScriptableAssetData> LoadScriptableAssetGraph(AssetReference reference)
	{
		ScriptableAssetGraphArchive archive = NativeSerializationBindings.LoadScriptableAssetGraph(NativeAssetReference.From(reference));
		if (archive.Version != 1) throw new InvalidOperationException("Unsupported ScriptableAsset graph archive version.");
		using JsonDocument response = JsonDocument.Parse(archive.Json);
		var data = new List<ScriptableAssetData>();
		foreach (JsonElement record in response.RootElement.EnumerateArray())
		{
			AssetReference identity = new(record.GetProperty("guid").GetString()!, record.GetProperty("localFileId").GetUInt64());
			string typeId = record.GetProperty("typeId").GetString()!;
			uint typeVersion = record.GetProperty("typeVersion").GetUInt32();
			data.Add(new ScriptableAssetData(identity, typeId, typeVersion, record.GetProperty("payload").GetRawText()));
		}
		return data;
	}

	internal static void Bind(ulong id)
	{
		RequireThread();
		Ursa.Rendering.RenderPipelineManager.RequireSessionBoundary();
		if (!sessions.TryGetValue(id, out Session? session)) throw new InvalidOperationException("Script session is closed or unknown.");
		BindingSnapshot previous = session.bindings;
		ulong previousSelection = BehaviourRegistry.currentSession;
		session.bindings = BindingRegistry.Fork(id);
		try { Select(id); }
		catch
		{
			BindingRegistry.Invalidate(session.bindings);
			session.bindings = previous;
			if (previousSelection != 0) Select(previousSelection);
			throw;
		}
		BindingRegistry.Invalidate(previous);
	}

	internal static void LoadAssembly(byte[] bytes)
	{
		RequireThread();
		ProjectContext context = BehaviourRegistry.currentSession == 0
			? legacyContext ??= new ProjectContext(0, false) : sessions[BehaviourRegistry.currentSession].context;
		using var reader = new PEReader(new MemoryStream(bytes, writable: false));
		MetadataReader metadata = reader.GetMetadataReader();
		string name = metadata.GetString(metadata.GetAssemblyDefinition().Name);
		if (name.StartsWith("Ursa.Editor", StringComparison.Ordinal) && !context.editor)
			throw new InvalidOperationException("Game sessions cannot load Editor assemblies.");
		if (name == "Ursa.Runtime" || name == "Ursa.Rendering" || name == "Ursa.ManagedHost" || frameworkNames.Contains(name))
			throw new InvalidOperationException("Project content cannot replace shared host or framework assemblies.");
		if (context.images.TryGetValue(name, out byte[]? previous))
		{
			if (!previous.AsSpan().SequenceEqual(bytes)) throw new InvalidOperationException($"Project assembly '{name}' changed within a loaded session.");
			return;
		}
		context.images.Add(name, bytes);
		// Validate separately so dependencies may be registered in any manifest order.
		if (BehaviourRegistry.currentSession == 0)
		{
			context.LoadFromAssemblyName(new AssemblyName(name));
			BehaviourRegistry.typeResolver = context.ResolveType;
			BehaviourRegistry.assemblyResolver = () => context.assemblies.Values.Append(typeof(Behaviour).Assembly);
		}
	}

	private static string Fingerprint(byte[] bytes)
	{
		ulong hash = 14695981039346656037;
		foreach (byte value in bytes) { hash ^= value; hash *= 1099511628211; }
		return hash.ToString("x16");
	}

	internal static void LoadContent(ulong id, string text)
	{
		Select(id);
		Session session = sessions[id];
		using JsonDocument manifest = JsonDocument.Parse(text);
		JsonElement root = manifest.RootElement;
		string? group = root.GetProperty("Group").GetString();
		if (root.GetProperty("Version").GetUInt32() != 2 || group is not ("Game" or "Editor") ||
		    (group == "Editor" && !session.context.editor))
			throw new InvalidOperationException("Content manifest version/group is not supported by this session.");
		BindingContent.ValidateRequirements(root.GetProperty("RequiredBindings").EnumerateArray().Select(module => new BindingRequirement(
			module.GetProperty("Id").GetUInt64(), module.GetProperty("Name").GetString()!, module.GetProperty("Hash").GetString()!,
			module.GetProperty("FunctionCount").GetUInt32(), module.GetProperty("CallScoped").GetBoolean())), group == "Editor");
		foreach (JsonElement record in root.GetProperty("Assemblies").EnumerateArray())
		{
			string path = record.GetProperty("Path").GetString() ?? throw new InvalidOperationException("Assembly path is empty.");
			byte[] image = EngineApi.ReadFile(path);
			if (Fingerprint(image) != record.GetProperty("Fingerprint").GetString()) throw new InvalidOperationException($"Assembly content fingerprint differs: {path}");
			string expectedName = record.GetProperty("Name").GetString() ?? string.Empty;
			using (var pe = new PEReader(new MemoryStream(image, writable: false)))
			{
				MetadataReader metadata = pe.GetMetadataReader();
				if (metadata.GetString(metadata.GetAssemblyDefinition().Name) != expectedName)
					throw new InvalidOperationException($"Assembly identity differs: {path}");
			}
			LoadAssembly(image);
			if (record.TryGetProperty("PdbPath", out JsonElement pdbPath))
			{
				byte[] pdb = EngineApi.ReadFile(pdbPath.GetString()!);
				if (Fingerprint(pdb) != record.GetProperty("PdbFingerprint").GetString()) throw new InvalidOperationException("PDB content fingerprint differs.");
				ScriptDebugSymbols.Validate(image, pdb);
				if (session.context.symbols.TryGetValue(expectedName, out byte[]? previousSymbols) && !previousSymbols.AsSpan().SequenceEqual(pdb))
					throw new InvalidOperationException($"Project symbols '{expectedName}' changed within a loaded session.");
				session.context.symbols[expectedName] = pdb;
			}
		}
		string catalogPath = root.GetProperty("TypeCatalog").GetString() ?? throw new InvalidOperationException("Script catalog path is empty.");
		byte[] catalogBytes = EngineApi.ReadFile(catalogPath);
		if (Fingerprint(catalogBytes) != root.GetProperty("CatalogFingerprint").GetString()) throw new InvalidOperationException("Script catalog content fingerprint differs.");
		using JsonDocument catalog = JsonDocument.Parse(catalogBytes);
		if (catalog.RootElement.GetProperty("Version").GetUInt32() != 1) throw new InvalidOperationException("Unsupported script catalog version.");
		var stableIds = new HashSet<string>(session.identities.Values.Select(value => value.Id), StringComparer.Ordinal);
		var localIds = new HashSet<string>(session.identities.Values.Select(value => value.SourceGuid + ":" + value.LocalFileId), StringComparer.Ordinal);
		foreach (JsonElement record in catalog.RootElement.GetProperty("Scripts").EnumerateArray())
		{
			string recordGroup = record.GetProperty("Group").GetString()!;
			if (recordGroup == "Editor" && (!session.context.editor || root.GetProperty("Group").GetString() != "Editor")) continue;
			if (recordGroup is not ("Game" or "Editor")) throw new InvalidOperationException("Invalid script catalog assembly group.");
			string typeName = record.GetProperty("ClrTypeName").GetString()!;
			Type type = session.context.ResolveType(typeName) ?? throw new InvalidOperationException($"Script type is missing: {typeName}");
			string kind = record.TryGetProperty("Kind", out JsonElement kindValue) ? kindValue.GetString()! : "Behaviour";
			bool supported = kind == "Behaviour" ? typeof(Behaviour).IsAssignableFrom(type) :
				kind == "ScriptableObject" ? typeof(ScriptableObject).IsAssignableFrom(type) :
				(kind is "ScriptedImporter" or "AssetPostprocessor") && recordGroup == "Editor" && sharedEditorAssembly != null &&
				sharedEditorAssembly.GetType("Ursa.Editor." + kind, throwOnError: true)!.IsAssignableFrom(type);
			if (!supported || type.IsAbstract || type.ContainsGenericParameters)
				throw new InvalidOperationException($"Script catalog type kind differs: {typeName}.");
			string stableId = record.GetProperty("ScriptType").GetString()!;
			uint version = record.GetProperty("Version").GetUInt32();
			string source = record.GetProperty("SourceGuid").GetString()!;
			ulong localId = record.GetProperty("LocalFileId").GetUInt64();
			if (recordGroup == "Game") session.runtimeTypes.Add(type);
			if (session.identities.TryGetValue(type.FullName!, out var existing))
			{
				if (existing.Id != stableId || existing.Version != version || existing.SourceGuid != source || existing.LocalFileId != localId)
					throw new InvalidOperationException($"Script identity changed within a loaded session: {typeName}");
				continue;
			}
			if (string.IsNullOrWhiteSpace(stableId) || version == 0 || !Guid.TryParse(source, out _) || localId == 0 ||
			    !stableIds.Add(stableId) || !localIds.Add(source + ":" + localId))
				throw new InvalidOperationException("Invalid or duplicate script asset identity.");
			session.identities.Add(type.FullName!, new ScriptIdentity(stableId, version, source, localId));
		}
		Validate(id);
	}

	internal static void RequirePublishableImportType(ulong id, Type type)
	{
		RequireImport(id);
		if (!sessions[id].runtimeTypes.Contains(type))
			throw new InvalidOperationException($"Import output type '{type.FullName}' is not in the Game type catalog; Editor types cannot be published.");
	}

	internal static ScriptedImporterInfo[] PrepareImportExtensions(ulong id, string[] nativeExtensions)
	{
		RequireThread();
		if (ImportExecution.Active) throw new InvalidOperationException("Import registration cannot change during an invocation.");
		Select(id);
		Session session = sessions[id];
		if (!session.importing || sharedEditorAssembly == null) throw new InvalidOperationException("Importer discovery requires an independent Import session and the shared Editor API.");
		if (session.importExtensions != null) throw new InvalidOperationException("Import registrations are already frozen for this session.");
		Type registry = sharedEditorAssembly.GetType("Ursa.Editor.ImportRegistry", throwOnError: true)!;
		MethodInfo factory = registry.GetMethod("Create", BindingFlags.Static | BindingFlags.NonPublic)!;
		session.importExtensions = (IImportExtensionSession)factory.Invoke(null, new object[] { session.context.assemblies.Values.ToArray(), nativeExtensions })!;
		return session.importExtensions.DescribeImporters();
	}

	internal static void InvokeScriptedImporter(ulong id, string identity)
	{
		RequireImport(id);
		(sessions[id].importExtensions ?? throw new InvalidOperationException("Import registrations have not been prepared.")).ImportAsset(identity);
	}

	internal static AssetPostprocessorInfo[] DescribeAssetPostprocessors(ulong id)
	{
		RequireThread();
		if (ImportExecution.Active) throw new InvalidOperationException("Cannot discover postprocessors during an import invocation.");
		Select(id);
		return (sessions[id].importExtensions ?? throw new InvalidOperationException("Import registrations have not been prepared.")).DescribePostprocessors();
	}

	internal static void InvokeAssetPostprocessor(ulong id, string identity)
	{
		RequireImport(id);
		(sessions[id].importExtensions ?? throw new InvalidOperationException("Import registrations have not been prepared.")).ProcessAsset(identity);
	}

	private static void RequireInteractiveSession(ulong id)
	{
		RequireThread();
		if (ImportExecution.Active || (sessions.TryGetValue(id, out var session) && session.importing))
			throw new InvalidOperationException("Independent Import sessions cannot perform GUI, Play or rendering operations.");
	}

	internal static void PrepareExtensions(ulong id)
	{
		RequireInteractiveSession(id);
		Select(id);
		Session session = sessions[id];
		if (!session.context.editor || sharedEditorAssembly == null) throw new InvalidOperationException("Editor extensions require the shared Editor API.");
		if (session.extensions != null) throw new InvalidOperationException("This editing session already owns its extension registrations.");
		Type registry = sharedEditorAssembly.GetType("Ursa.Editor.ExtensionRegistry", throwOnError: true)!;
		MethodInfo factory = registry.GetMethod("Create", BindingFlags.Static | BindingFlags.NonPublic)!;
		session.extensions = (IEditorExtensionSession)factory.Invoke(null, new object[] { session.context.assemblies.Values.ToArray() })!;
	}

	internal static EditorExtensionInfo[] DescribeExtensions(ulong id)
	{
		Select(id);
		return sessions[id].extensions?.Describe() ?? throw new InvalidOperationException("Editor extensions have not been prepared.");
	}

	internal static bool InvokeExtension(ulong id, string extension)
	{
		RequireInteractiveSession(id);
		RequireThread();
		if (BehaviourRegistry.currentSession != id) throw new InvalidOperationException("Editor callback requires its selected script session.");
		return sessions[id].extensions?.Invoke(extension) ?? false;
	}

	internal static EditorPreviewResult InvokePreview(ulong id, string extension, ulong instance, EditorPreviewPhase phase, Rect rect)
	{
		RequireInteractiveSession(id);
		RequireThread();
		if (BehaviourRegistry.currentSession != id) throw new InvalidOperationException("Preview requires its selected script session.");
		return sessions[id].extensions?.Preview(extension, instance, phase, rect)
			?? throw new InvalidOperationException("Editor extensions have not been prepared.");
	}

	internal static void PrepareReload(ulong id, string snapshots)
	{
		RequireInteractiveSession(id);
		Select(id);
		Session session = sessions[id];
		if (!session.context.editor) throw new InvalidOperationException("Editing snapshots require an editing session.");
		using JsonDocument input = JsonDocument.Parse(snapshots);
		var outputs = new List<object>();
		foreach (JsonElement record in input.RootElement.EnumerateArray())
		{
			JsonElement payload = record.GetProperty("Payload");
			string stableType = payload.GetProperty("ScriptType").GetString()!;
			string? typeName = session.identities.FirstOrDefault(pair => pair.Value.Id == stableType).Key;
			if (typeName == null) throw new InvalidOperationException($"Missing script '{stableType}' on component {record.GetProperty("Id").GetString()}.");
			Type type = session.context.ResolveType(typeName)!;
			if (Activator.CreateInstance(type) is not Behaviour instance) throw new InvalidOperationException("Cannot construct the candidate script.");
			ScriptFieldSchema.Deserialize(instance, payload.GetRawText());
			using JsonDocument normalized = JsonDocument.Parse(ScriptFieldSchema.Serialize(instance));
			var fields = normalized.RootElement.GetProperty("Fields").EnumerateObject().ToDictionary(field => field.Name, field => field.Value.Clone());
			var oldFields = payload.GetProperty("Fields").EnumerateObject().ToDictionary(field => field.Name, field => field.Value.Clone());
			var formerNames = new HashSet<string>(StringComparer.Ordinal);
			foreach (ScriptFieldDescriptor descriptor in ScriptFieldSchema.Describe(type))
			{
				foreach (string former in descriptor.FormerNames) formerNames.Add(former);
				if (descriptor.Kind is ScriptSerializedFieldKind.GameObjectReference or ScriptSerializedFieldKind.ComponentReference ||
				    descriptor.ElementKind is ScriptSerializedFieldKind.GameObjectReference or ScriptSerializedFieldKind.ComponentReference)
				{
					if (oldFields.TryGetValue(descriptor.Name, out JsonElement reference)) fields[descriptor.Name] = reference;
					else foreach (string former in descriptor.FormerNames)
						if (oldFields.TryGetValue(former, out reference)) { fields[descriptor.Name] = reference; break; }
				}
			}
			foreach (var oldField in oldFields)
				if (!fields.ContainsKey(oldField.Key) && !formerNames.Contains(oldField.Key)) fields.Add(oldField.Key, oldField.Value);
			var result = normalized.RootElement.EnumerateObject().ToDictionary(field => field.Name, field => (object)field.Value.Clone());
			result["Fields"] = fields;
			outputs.Add(new { Id = record.GetProperty("Id").GetString(),
				TypeName = type.FullName + ", " + type.Assembly.GetName().Name, Payload = result });
		}
		session.preparedReload = JsonSerializer.Serialize(outputs);
	}

	internal static string PreparedReload(ulong id)
	{
		Select(id);
		return sessions[id].preparedReload;
	}

	internal static void TransferAssets(ulong source, ulong candidate)
	{
		RequireInteractiveSession(source);
		RequireInteractiveSession(candidate);
		RequireThread();
		Ursa.Rendering.RenderPipelineManager.RequireSessionBoundary();
		if (source == candidate || !sessions.TryGetValue(source, out var oldSession) ||
		    !sessions.TryGetValue(candidate, out var newSession) || !oldSession.context.editor || !newSession.context.editor)
			throw new InvalidOperationException("Asset reload requires distinct editing sessions.");
		Select(source);
		var selection = Ursa.Rendering.RenderPipelineManager.ReloadSelection();
		var descriptions = ScriptableObjectRegistry.CaptureAssets();
		ScriptableAssetData? transient = null;
		AssetReference? persistent = selection.Asset?.AssetReference;
		if (selection.Asset != null && persistent == null)
		{
			ScriptIdentity identity = BehaviourRegistry.IdentityFor(selection.Asset.GetType());
			transient = new ScriptableAssetData(default, identity.Id, identity.Version,
				ScriptFieldSchema.Serialize(selection.Asset));
		}
		try
		{
			Select(candidate);
			ScriptableObjectRegistry.RestoreAssets(descriptions);
			Ursa.Rendering.RenderPipelineAsset? asset = null;
			if (persistent.HasValue)
				asset = (Ursa.Rendering.RenderPipelineAsset)ScriptableObjectRegistry.ResolveAsset(persistent.Value,
					typeof(Ursa.Rendering.RenderPipelineAsset));
			else if (transient != null)
			{
				Type type = ScriptableObjectRegistry.ResolveAssetType(transient);
				asset = (Ursa.Rendering.RenderPipelineAsset)ScriptableObject.CreateForLoad(type);
				ScriptFieldSchema.Deserialize(asset, transient.Payload, ScriptableObjectRegistry.ResolveAsset);
				ScriptFieldSchema.ResolvePendingReferences(asset);
				asset.ValidateFields();
				asset.Activate();
			}
			if (selection.HasSelection)
			{
				Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(asset);
				Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges();
			}
		}
		catch
		{
			Select(source);
			throw;
		}
	}

	internal static void ConfigurePipeline(ulong id, string json)
	{
		using JsonDocument document = JsonDocument.Parse(json);
		AssetReference? reference = null;
		if (document.RootElement.ValueKind != JsonValueKind.Null)
		{
			reference = new AssetReference(document.RootElement.GetProperty("guid").GetString()!,
				document.RootElement.GetProperty("localFileId").GetUInt64());
		}
		ConfigurePipeline(id, reference);
	}

	internal static void ConfigurePipeline(ulong id, AssetReference? reference)
	{
		RequireInteractiveSession(id);
		Select(id);
		Ursa.Rendering.RenderPipelineAsset? asset = reference.HasValue
			? ScriptableObject.Load<Ursa.Rendering.RenderPipelineAsset>(reference.Value) : null;
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(asset);
		Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges();
	}

	internal static void RenderScene(ulong id, float aspect)
	{
		RequireInteractiveSession(id);
		Select(id);
		Ursa.Rendering.RenderPipelineManager.RenderScene(aspect);
	}

	internal static void Validate(ulong id)
	{
		Select(id);
		ProjectContext context = sessions[id].context;
		foreach (string name in context.images.Keys.ToArray())
		{
			Assembly assembly = context.LoadFromAssemblyName(new AssemblyName(name));
			foreach (AssemblyName dependency in assembly.GetReferencedAssemblies()) context.LoadFromAssemblyName(dependency);
			foreach (Type type in assembly.GetTypes())
			{
				if (!type.IsAbstract && !type.ContainsGenericParameters &&
				    (typeof(Behaviour).IsAssignableFrom(type) || typeof(ScriptableObject).IsAssignableFrom(type)))
				{
					if (typeof(Behaviour).IsAssignableFrom(type) && type.GetConstructor(Type.EmptyTypes) == null)
						throw new InvalidOperationException($"Behaviour '{type.FullName}' needs a public default constructor.");
					foreach (ScriptFieldDescriptor field in ScriptFieldSchema.Describe(type))
						if (!field.IsSupported) throw new InvalidOperationException(field.Diagnostic);
				}
			}
		}
		BehaviourRegistry.ResolveSessionReferences(id);
	}

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static WeakReference Release(ulong id)
	{
		Session session = sessions[id];
		Select(id);
		try { Ursa.Rendering.RenderPipelineManager.ReleaseSession(id); }
		catch (Exception error) { Console.Error.WriteLine($"Render pipeline session cleanup failed: {error}"); }
		foreach (Exception error in ScriptableObjectRegistry.ClearSession(id))
			Console.Error.WriteLine($"ScriptableObject session cleanup failed: {error}");
		session.extensions?.Dispose();
		session.extensions = null;
		session.importExtensions?.Dispose();
		session.importExtensions = null;
		sessions.Remove(id);
		BehaviourRegistry.ReleaseSession(id);
		BehaviourRegistry.typeResolver = null;
		BehaviourRegistry.assemblyResolver = null;
		BehaviourRegistry.identityResolver = null;
		BehaviourRegistry.currentSession = 0;
		ScriptableObjectRegistry.SelectSession(0);
		BindingRegistry.Invalidate(session.bindings);
		session.context.assemblies.Clear();
		session.context.images.Clear();
		session.context.symbols.Clear();
		session.identities.Clear();
		session.runtimeTypes.Clear();
		var weak = new WeakReference(session.context);
		session.context.Unload();
		return weak;
	}

	internal static bool Close(ulong id)
	{
		RequireThread();
		if (ImportExecution.Active)
		{
			ImportScopeValue active = ImportExecution.Current;
			if (active.Session != id) throw new InvalidOperationException("Import callbacks cannot close another script session.");
			ImportExecution.End(active.Session, active.Generation, active.Invocation);
		}
		Ursa.Rendering.RenderPipelineManager.RequireSessionBoundary();
		if (!sessions.ContainsKey(id)) throw new InvalidOperationException("Script session is already closed.");
		WeakReference weak = Release(id);
		for (int attempt = 0; attempt < 8 && weak.IsAlive; ++attempt)
		{
			GC.Collect();
			GC.WaitForPendingFinalizers();
			GC.Collect();
		}
		if (weak.IsAlive) faulted = true;
		return !weak.IsAlive;
	}

	internal static void CloseOwnerSessions()
	{
		RequireThread();
		List<Exception>? errors = null;
		if (ImportExecution.Active)
		{
			ImportScopeValue active = ImportExecution.Current;
			try { ImportExecution.End(active.Session, active.Generation, active.Invocation); }
			catch (Exception error) { (errors ??= new()).Add(error); }
		}
		foreach (ulong id in sessions.Keys.ToArray())
		{
			try
			{
				if (!Close(id)) throw new InvalidOperationException("An owned script session did not unload.");
			}
			catch (Exception error) { (errors ??= new()).Add(error); }
		}
		Ursa.Rendering.RenderPipelineManager.ReleaseSession(0);
		foreach (Exception error in ScriptableObjectRegistry.ClearSession(0)) (errors ??= new()).Add(error);
		BehaviourRegistry.ReleaseSession(0);
		BehaviourRegistry.typeResolver = null;
		BehaviourRegistry.assemblyResolver = null;
		BehaviourRegistry.identityResolver = null;
		BehaviourRegistry.currentSession = 0;
		ScriptableObjectRegistry.SelectSession(0);
		legacyContext?.assemblies.Clear();
		legacyContext?.images.Clear();
		legacyContext?.symbols.Clear();
		legacyContext?.Unload();
		legacyContext = null;
		if (errors != null) throw new AggregateException("Owned script session cleanup failed.", errors);
	}
}
