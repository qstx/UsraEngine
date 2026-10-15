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
using Ursa;

namespace Ursa.ManagedHost;

// Stable bridge entries survive; project contexts and registrations are session owned.
internal static unsafe class ScriptSessions
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
			return null;
		}
	}

	private sealed class Session
	{
		internal readonly ProjectContext context;
		internal NativeApiV6 api;
		internal IEditorExtensionSession? extensions;
		internal string preparedReload = string.Empty;
		internal readonly Dictionary<string, ScriptIdentity> identities = new(StringComparer.Ordinal);

		internal Session(ulong id, bool editor, NativeApiV6 api)
		{
			context = new ProjectContext(id, editor);
			this.api = api;
		}
	}

	private static readonly HashSet<string> frameworkNames = new(
		((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") ?? string.Empty)
		.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
		.Select(path => Path.GetFileNameWithoutExtension(path)).Where(name => name.StartsWith("System.", StringComparison.Ordinal) ||
			name.StartsWith("Microsoft.", StringComparison.Ordinal) || name is "System" or "mscorlib" or "netstandard"), StringComparer.OrdinalIgnoreCase);
	private static readonly Dictionary<ulong, Session> sessions = new();
	private static ulong nextId = 1;
	private static int ownerThread;
	private static bool faulted;
	private static ProjectContext? legacyContext;
	internal static Assembly? sharedEditorAssembly;

	internal static void RequireThread()
	{
		if (ownerThread == 0) ownerThread = Environment.CurrentManagedThreadId;
		if (ownerThread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Script sessions require the host main thread.");
	}

	internal static ulong Create(bool editor, NativeApiV6 api)
	{
		RequireThread();
		Ursa.Rendering.RenderPipelineManager.RequireSessionBoundary();
		if (faulted) throw new InvalidOperationException("A project context did not unload. Restart before creating another script session.");
		ulong id = nextId++;
		sessions.Add(id, new Session(id, editor, api));
		Select(id);
		return id;
	}

	internal static void Select(ulong id)
	{
		RequireThread();
		if (!sessions.TryGetValue(id, out Session? session)) throw new InvalidOperationException("Script session is closed or unknown.");
		Ursa.Rendering.RenderPipelineManager.SelectSession(id);
		BehaviourRegistry.currentSession = id;
		ScriptableObjectRegistry.SelectSession(id);
		ScriptableObjectRegistry.graphLoader = LoadScriptableAssetGraph;
		BehaviourRegistry.typeResolver = session.context.ResolveType;
		BehaviourRegistry.identityResolver = type => session.identities.TryGetValue(type.FullName ?? type.Name, out ScriptIdentity identity)
			? identity : BehaviourRegistry.DeclaredIdentity(type);
		BehaviourRegistry.assemblyResolver = () => session.context.assemblies.Values.Append(typeof(Behaviour).Assembly);
			NativeApiV6 api = session.api;
		EngineApi.Initialize((nint)(&api));
	}

	private static IReadOnlyList<ScriptableAssetData> LoadScriptableAssetGraph(AssetReference reference)
	{
		using JsonDocument response = JsonDocument.Parse(EngineApi.ResourceRequest(new
		{
			operation = "scriptableasset.graph.load", guid = reference.Guid, localFileId = reference.LocalFileId
		}).GetRawText());
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

	internal static void Bind(ulong id, NativeApiV6 api)
	{
		RequireThread();
		Ursa.Rendering.RenderPipelineManager.RequireSessionBoundary();
		if (!sessions.TryGetValue(id, out Session? session)) throw new InvalidOperationException("Script session is closed or unknown.");
		session.api = api;
		Select(id);
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
		if (!context.images.TryAdd(name, bytes)) throw new InvalidOperationException($"Duplicate project assembly '{name}'.");
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
		if (root.GetProperty("Version").GetUInt32() != 1 || group is not ("Game" or "Editor") ||
		    (group == "Editor" && !session.context.editor))
			throw new InvalidOperationException("Content manifest version/group is not supported by this session.");
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
				session.context.symbols.Add(expectedName, pdb);
			}
		}
		string catalogPath = root.GetProperty("TypeCatalog").GetString() ?? throw new InvalidOperationException("Script catalog path is empty.");
		byte[] catalogBytes = EngineApi.ReadFile(catalogPath);
		if (Fingerprint(catalogBytes) != root.GetProperty("CatalogFingerprint").GetString()) throw new InvalidOperationException("Script catalog content fingerprint differs.");
		using JsonDocument catalog = JsonDocument.Parse(catalogBytes);
		if (catalog.RootElement.GetProperty("Version").GetUInt32() != 1) throw new InvalidOperationException("Unsupported script catalog version.");
		var stableIds = new HashSet<string>(StringComparer.Ordinal);
		var localIds = new HashSet<string>(StringComparer.Ordinal);
		foreach (JsonElement record in catalog.RootElement.GetProperty("Scripts").EnumerateArray())
		{
			if (record.GetProperty("Group").GetString() != "Game") continue;
			string typeName = record.GetProperty("ClrTypeName").GetString()!;
			Type type = session.context.ResolveType(typeName) ?? throw new InvalidOperationException($"Script type is missing: {typeName}");
			string kind = record.TryGetProperty("Kind", out JsonElement kindValue) ? kindValue.GetString()! : "Behaviour";
			if ((kind != "Behaviour" && kind != "ScriptableObject") || type.IsAbstract || type.ContainsGenericParameters ||
			    !(kind == "Behaviour" ? typeof(Behaviour).IsAssignableFrom(type) : typeof(ScriptableObject).IsAssignableFrom(type)))
				throw new InvalidOperationException($"Script catalog type kind differs: {typeName}.");
			string stableId = record.GetProperty("ScriptType").GetString()!;
			uint version = record.GetProperty("Version").GetUInt32();
			string source = record.GetProperty("SourceGuid").GetString()!;
			ulong localId = record.GetProperty("LocalFileId").GetUInt64();
			if (string.IsNullOrWhiteSpace(stableId) || version == 0 || !Guid.TryParse(source, out _) || localId == 0 ||
			    !stableIds.Add(stableId) || !localIds.Add(source + ":" + localId))
				throw new InvalidOperationException("Invalid or duplicate script asset identity.");
			UrsaScriptTypeAttribute? declared = type.GetCustomAttribute<UrsaScriptTypeAttribute>();
			if (declared != null && (declared.Id != stableId || declared.Version != version))
				throw new InvalidOperationException($"Script identity no longer matches the catalog: {typeName}");
			session.identities.Add(type.FullName!, new ScriptIdentity(stableId, version, source, localId));
		}
		Validate(id);
	}

	internal static void PrepareExtensions(ulong id)
	{
		Select(id);
		Session session = sessions[id];
		if (!session.context.editor || sharedEditorAssembly == null) throw new InvalidOperationException("Editor extensions require the shared Editor API.");
		if (session.extensions != null) throw new InvalidOperationException("This editing session already owns its extension registrations.");
		Type registry = sharedEditorAssembly.GetType("Ursa.Editor.ExtensionRegistry", throwOnError: true)!;
		MethodInfo factory = registry.GetMethod("Create", BindingFlags.Static | BindingFlags.NonPublic)!;
		session.extensions = (IEditorExtensionSession)factory.Invoke(null, new object[] { session.context.assemblies.Values.ToArray() })!;
	}

	internal static string DescribeExtensions(ulong id)
	{
		Select(id);
		return sessions[id].extensions?.Describe() ?? throw new InvalidOperationException("Editor extensions have not been prepared.");
	}

	internal static bool InvokeExtension(ulong id, string extension, nint api, int size)
	{
		Select(id);
		return sessions[id].extensions?.Invoke(extension, api, size) ?? false;
	}

	internal static void PrepareReload(ulong id, string snapshots)
	{
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
		Select(id);
		using JsonDocument document = JsonDocument.Parse(json);
		Ursa.Rendering.RenderPipelineAsset? asset = null;
		if (document.RootElement.ValueKind != JsonValueKind.Null)
		{
			var reference = new AssetReference(document.RootElement.GetProperty("guid").GetString()!,
				document.RootElement.GetProperty("localFileId").GetUInt64());
			asset = ScriptableObject.Load<Ursa.Rendering.RenderPipelineAsset>(reference);
		}
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(asset);
		Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges();
	}

	internal static void RenderScene(ulong id, float aspect)
	{
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
		sessions.Remove(id);
		BehaviourRegistry.ReleaseSession(id);
		BehaviourRegistry.typeResolver = null;
		BehaviourRegistry.assemblyResolver = null;
		BehaviourRegistry.identityResolver = null;
		BehaviourRegistry.currentSession = 0;
		ScriptableObjectRegistry.SelectSession(0);
		EngineApi.Clear();
		session.context.assemblies.Clear();
		session.context.images.Clear();
		session.context.symbols.Clear();
		session.identities.Clear();
		var weak = new WeakReference(session.context);
		session.context.Unload();
		return weak;
	}

	internal static bool Close(ulong id)
	{
		RequireThread();
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
}

public static unsafe partial class Bootstrap
{
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int ConfigureRenderPipeline(ulong id, byte* json, ulong length)
	{
		if (json == null || length == 0 || length > 4096) return 1;
		try { ScriptSessions.ConfigurePipeline(id, Encoding.UTF8.GetString(new ReadOnlySpan<byte>(json, (int)length))); return 0; }
		catch (Exception error) { Console.Error.WriteLine($"管线配置失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int RenderScene(ulong id, float aspect)
	{
		try { ScriptSessions.RenderScene(id, aspect); return 0; }
		catch (Exception error) { Console.Error.WriteLine($"管线渲染失败：{error}"); return 2; }
	}
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int TransferScriptableAssets(ulong source, ulong candidate)
	{
		try { ScriptSessions.TransferAssets(source, candidate); return 0; }
		catch (Exception error) { Console.Error.WriteLine($"候选管线资产迁移失败：{error}"); return 2; }
	}
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int PrepareEditorExtensions(ulong id)
	{
		try { ScriptSessions.PrepareExtensions(id); return 0; }
		catch (Exception error) { Console.Error.WriteLine($"候选编辑器扩展注册失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int DescribeEditorExtensions(ulong id, byte* output, ulong capacity, ulong* required)
	{
		if (required == null || (output == null && capacity != 0)) return 1;
		try
		{
			byte[] bytes = Encoding.UTF8.GetBytes(ScriptSessions.DescribeExtensions(id));
			*required = (ulong)bytes.Length;
			if (capacity < (ulong)bytes.Length) return 4;
			bytes.AsSpan().CopyTo(new Span<byte>(output, bytes.Length));
			return 0;
		}
		catch (Exception error) { Console.Error.WriteLine($"编辑器扩展目录读取失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int InvokeEditorExtension(ulong id, byte* extension, ulong length, nint api, int size)
	{
		if (extension == null || length == 0 || length > 4096 || api == 0) return 1;
		try { return ScriptSessions.InvokeExtension(id, Encoding.UTF8.GetString(new ReadOnlySpan<byte>(extension, (int)length)), api, size) ? 0 : 2; }
		catch (Exception error) { Console.Error.WriteLine($"编辑器扩展调用失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int PrepareScriptReload(ulong id, byte* text, ulong length)
	{
		if (text == null || length == 0 || length > 64ul * 1024 * 1024) return 1;
		try { ScriptSessions.PrepareReload(id, Encoding.UTF8.GetString(new ReadOnlySpan<byte>(text, (int)length))); return 0; }
		catch (Exception error) { Console.Error.WriteLine($"候选脚本字段恢复失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int ReadPreparedScriptReload(ulong id, byte* output, ulong capacity, ulong* required)
	{
		if (required == null || (output == null && capacity != 0)) return 1;
		try
		{
			byte[] bytes = Encoding.UTF8.GetBytes(ScriptSessions.PreparedReload(id));
			*required = (ulong)bytes.Length;
			if (capacity < (ulong)bytes.Length) return 4;
			bytes.AsSpan().CopyTo(new Span<byte>(output, bytes.Length));
			return 0;
		}
		catch (Exception error) { Console.Error.WriteLine($"候选脚本字段读取失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int LoadEditorApi(byte* image, ulong length)
	{
		if (image == null || length == 0 || length > 256ul * 1024 * 1024) return 1;
		try
		{
			ScriptSessions.RequireThread();
			if (ScriptSessions.sharedEditorAssembly != null) return 0;
			using var stream = new MemoryStream(new ReadOnlySpan<byte>(image, (int)length).ToArray(), writable: false);
			AssemblyLoadContext context = AssemblyLoadContext.GetLoadContext(typeof(Behaviour).Assembly)!;
			Assembly assembly = context.LoadFromStream(stream);
			if (assembly.GetName().Name != "Ursa.Editor") throw new InvalidOperationException("Shared editor API identity is invalid.");
			ScriptSessions.sharedEditorAssembly = assembly;
			return 0;
		}
		catch (Exception error) { Console.Error.WriteLine($"编辑器 API 加载失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int LoadScriptContent(ulong id, byte* text, ulong length)
	{
		if (text == null || length == 0 || length > 16ul * 1024 * 1024) return 1;
		try { ScriptSessions.LoadContent(id, Encoding.UTF8.GetString(new ReadOnlySpan<byte>(text, (int)length))); return 0; }
		catch (Exception error) { Console.Error.WriteLine($"脚本内容清单加载失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int CreateScriptSession(uint kind, nint api, int size, ulong* output)
	{
		if (kind > 1 || output == null || Validate(api, size) != 0) return 1;
		try { *output = ScriptSessions.Create(kind == 1, *(Ursa.NativeApiV6*)api); return 0; }
		catch (Exception error) { Console.Error.WriteLine($"脚本会话创建失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int SelectScriptSession(ulong id)
	{
		try { ScriptSessions.Select(id); return 0; }
		catch (Exception error) { Console.Error.WriteLine($"脚本会话选择失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int BindScriptSession(ulong id, nint api, int size)
	{
		if (Validate(api, size) != 0) return 1;
		try { ScriptSessions.Bind(id, *(Ursa.NativeApiV6*)api); return 0; }
		catch (Exception error) { Console.Error.WriteLine($"脚本会话绑定失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int ValidateScriptSession(ulong id)
	{
		try { ScriptSessions.Validate(id); return 0; }
		catch (Exception error) { Console.Error.WriteLine($"脚本会话校验失败：{error}"); return 2; }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
	public static int CloseScriptSession(ulong id)
	{
		try { return ScriptSessions.Close(id) ? 0 : 3; }
		catch (Exception error) { Console.Error.WriteLine($"脚本会话卸载失败：{error}"); return 2; }
	}
}
