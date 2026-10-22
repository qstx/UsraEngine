using System;
using System.Collections.Generic;
using System.Threading;

namespace Ursa;

internal static class ScriptableObjectRegistry
{
	[ThreadStatic] private static Dictionary<ulong, Dictionary<ulong, ScriptableObject>>? ownerSessions;
	[ThreadStatic] private static HashSet<ulong>? ownerClosingSessions;
	[ThreadStatic] private static Dictionary<ulong, Dictionary<AssetReference, ScriptableObject>>? ownerPersistentAssets;
	private static Dictionary<ulong, Dictionary<ulong, ScriptableObject>> sessions => ownerSessions ??= new();
	private static HashSet<ulong> closingSessions => ownerClosingSessions ??= new();
	private static Dictionary<ulong, Dictionary<AssetReference, ScriptableObject>> persistentAssets => ownerPersistentAssets ??= new();
	private sealed class AssetLease
	{
		internal int count;
		internal bool owned;
	}
	[ThreadStatic] private static Dictionary<ulong, Dictionary<AssetReference, AssetLease>>? ownerAssetLeases;
	[ThreadStatic] private static Dictionary<ulong, Dictionary<AssetReference, ScriptableAssetGraph>>? ownerLoadedGraphs;
	private static Dictionary<ulong, Dictionary<AssetReference, AssetLease>> assetLeases => ownerAssetLeases ??= new();
	private static Dictionary<ulong, Dictionary<AssetReference, ScriptableAssetGraph>> loadedGraphs => ownerLoadedGraphs ??= new();
	[ThreadStatic] internal static Func<AssetReference, IReadOnlyList<ScriptableAssetData>>? graphLoader;
	[ThreadStatic] private static ulong currentSession;
	[ThreadStatic] internal static bool SnapshotOnly;
	private static long nextSnapshotSession;
	internal static IDisposable BeginEditorSnapshot()
	{
		RequireThread();
		if (SnapshotOnly) throw new InvalidOperationException("Editor object snapshots cannot be nested.");
		ulong previous = currentSession;
		var previousLoader = graphLoader;
		ulong temporary = checked((ulong)Interlocked.Increment(ref nextSnapshotSession)) | (1ul << 63);
		currentSession = temporary;
		SnapshotOnly = true;
		return new EditorSnapshotLease(previous, temporary, previousLoader);
	}
	private sealed class EditorSnapshotLease(ulong previous, ulong temporary,
		Func<AssetReference, IReadOnlyList<ScriptableAssetData>>? previousLoader) : IDisposable
	{
		private readonly int thread = Environment.CurrentManagedThreadId;
		private bool disposed;
		public void Dispose()
		{
			if (disposed) return;
			if (thread != Environment.CurrentManagedThreadId || currentSession != temporary)
				throw new InvalidOperationException("Editor object snapshot requires its owning callback.");
			disposed = true;
			try
			{
				var errors = ClearSession(temporary);
				if (errors.Count != 0) throw new AggregateException("Editor object snapshot cleanup failed.", errors);
			}
			finally { SnapshotOnly = false; currentSession = previous; graphLoader = previousLoader; }
		}
	}
	private static long nextInstanceId;
	[ThreadStatic] private static int ownerThread;

	internal static void RequireThread()
	{
		if (ownerThread == 0)
		{
			if (Ursa.Bindings.Generated.RuntimeModule.Context == 0)
				throw new InvalidOperationException("ScriptableObject APIs require an initialized host owner thread.");
			ownerThread = Environment.CurrentManagedThreadId;
		}
		if (ownerThread != Environment.CurrentManagedThreadId)
			throw new InvalidOperationException("ScriptableObject APIs require the host main thread.");
	}

	internal static void SelectSession(ulong session)
	{
		RequireThread();
		currentSession = session;
		if (session == 0) graphLoader = null;
	}

	internal static bool IsCurrentSession(ulong session) => ownerThread != 0 && currentSession == session &&
		Ursa.Bindings.Generated.RuntimeModule.Context != 0;

	internal static (ulong InstanceId, ulong SessionId) Register(ScriptableObject instance)
	{
		RequireThread();
		if (closingSessions.Contains(currentSession))
			throw new InvalidOperationException("Cannot create a ScriptableObject while its session is closing.");
		if (!sessions.TryGetValue(currentSession, out Dictionary<ulong, ScriptableObject>? objects))
		{
			objects = new Dictionary<ulong, ScriptableObject>();
			sessions.Add(currentSession, objects);
		}
		ulong id = checked((ulong)Interlocked.Increment(ref nextInstanceId));
		objects.Add(id, instance);
		return (id, currentSession);
	}

	internal static ScriptableObject RequireInstance(ulong id)
	{
		RequireThread();
		if (!sessions.TryGetValue(currentSession, out var objects) || !objects.TryGetValue(id, out var value))
			throw new InvalidOperationException("ScriptableObject belongs to another or completed session.");
		value.RequireValid();
		return value;
	}

	internal static void Unregister(ulong session, ulong id)
	{
		if (!sessions.TryGetValue(session, out Dictionary<ulong, ScriptableObject>? objects)) return;
		objects.Remove(id);
		if (persistentAssets.TryGetValue(session, out var assets))
		{
			var keys = new List<AssetReference>();
			foreach (var entry in assets)
				if (!objects.ContainsValue(entry.Value)) keys.Add(entry.Key);
			foreach (AssetReference key in keys)
			{
				assets.Remove(key);
				if (assetLeases.TryGetValue(session, out var leases))
				{
					leases.Remove(key);
					if (leases.Count == 0) assetLeases.Remove(session);
				}
			}
			if (assets.Count == 0) persistentAssets.Remove(session);
		}
		if (objects.Count == 0) sessions.Remove(session);
	}

	internal static void BindAsset(ScriptableObject instance, AssetReference reference)
	{
		RequireThread();
		if (string.IsNullOrEmpty(reference.Guid) || reference.LocalFileId == 0)
			throw new ArgumentException("A ScriptableObject asset needs a GUID and nonzero local file ID.");
		if (!persistentAssets.TryGetValue(currentSession, out var assets))
		{
			assets = new Dictionary<AssetReference, ScriptableObject>();
			persistentAssets.Add(currentSession, assets);
		}
		if (assets.TryGetValue(reference, out var previous) && !ReferenceEquals(previous, instance))
			throw new InvalidOperationException("Asset identity is already loaded in this session.");
		assets[reference] = instance;
	}

	internal static ScriptableObject ResolveAsset(AssetReference reference, Type expectedType)
	{
		RequireThread();
		if (!persistentAssets.TryGetValue(currentSession, out var assets) ||
		    !assets.TryGetValue(reference, out var instance))
			throw new InvalidOperationException($"ScriptableObject dependency is not loaded: {reference.Guid}:{reference.LocalFileId}.");
		instance.RequireValid();
		if (!expectedType.IsInstanceOfType(instance))
			throw new InvalidOperationException("ScriptableObject dependency type does not match its field.");
		return instance;
	}

	internal static ScriptableObject? FindAsset(AssetReference reference)
	{
		RequireThread();
		if (reference.LocalFileId == 0 && loadedGraphs.TryGetValue(currentSession, out var roots) && roots.TryGetValue(reference, out var graph))
			return graph.Get(graph.Root);
		if (persistentAssets.TryGetValue(currentSession, out var assets) && assets.TryGetValue(reference, out var instance))
		{
			instance.RequireValid();
			return instance;
		}
		return null;
	}

	internal static ScriptableObject LoadAsset(AssetReference reference, Type expectedType)
	{
		RequireThread();
		if (!typeof(ScriptableObject).IsAssignableFrom(expectedType))
			throw new ArgumentException("Asset references require a ScriptableObject field type.", nameof(expectedType));
		ScriptableObject? existing = FindAsset(reference);
		if (existing != null)
		{
			if (!expectedType.IsInstanceOfType(existing)) throw new InvalidOperationException("Loaded asset type differs from its field.");
			return existing;
		}
		if (currentSession == 0 || graphLoader == null)
			throw new InvalidOperationException("ScriptableAsset loading requires an active project asset catalog.");
		var descriptions = graphLoader(reference);
		var graph = ScriptableAssetGraph.Load(descriptions, ResolveAssetType);
		ScriptableObject root;
		try
		{
			root = graph.Get(reference.LocalFileId == 0 ? graph.Root : reference);
			if (!expectedType.IsInstanceOfType(root)) throw new InvalidOperationException("Loaded asset type differs from its field.");
			if (!loadedGraphs.TryGetValue(currentSession, out var roots))
			{
				roots = new Dictionary<AssetReference, ScriptableAssetGraph>();
				loadedGraphs.Add(currentSession, roots);
			}
			roots.Add(reference, graph);
			ImportExecution.RecordReadOnlyAssets(graph.Instances);
			return root;
		}
		catch
		{
			graph.Dispose();
			throw;
		}
	}

	internal static Type ResolveAssetType(ScriptableAssetData data)
	{
		foreach (var assembly in BehaviourRegistry.Assemblies)
		{
			Type[] candidates;
			try { candidates = assembly.GetTypes(); }
			catch (System.Reflection.ReflectionTypeLoadException error)
			{
				var available = new List<Type>();
				foreach (Type? candidate in error.Types) if (candidate != null) available.Add(candidate);
				candidates = available.ToArray();
			}
			foreach (Type type in candidates)
			{
				if (!type.IsAbstract && !type.ContainsGenericParameters && typeof(ScriptableObject).IsAssignableFrom(type))
				{
					ScriptIdentity identity;
					try { identity = BehaviourRegistry.IdentityFor(type); }
					catch (InvalidOperationException) { continue; }
					if (identity.Id == data.TypeId && identity.Version == data.TypeVersion)
					{
						if ((!string.IsNullOrEmpty(identity.SourceGuid) && identity.SourceGuid != dataSourceGuid(data.Payload)) ||
						    (identity.LocalFileId != 0 && identity.LocalFileId != dataSourceLocalId(data.Payload)))
							throw new InvalidOperationException($"ScriptableAsset source type identity differs for '{type.FullName}'.");
						return type;
					}
				}
			}
		}
		throw new InvalidOperationException($"Missing ScriptableObject type '{data.TypeId}' for asset {data.Reference.Guid}:{data.Reference.LocalFileId}.");
	}

	private static string dataSourceGuid(string payload)
	{
		using var document = System.Text.Json.JsonDocument.Parse(payload);
		return document.RootElement.GetProperty("SourceGuid").GetString() ?? string.Empty;
	}
	private static ulong dataSourceLocalId(string payload)
	{
		using var document = System.Text.Json.JsonDocument.Parse(payload);
		return document.RootElement.GetProperty("LocalFileId").GetUInt64();
	}

	internal static IReadOnlyList<ScriptableAssetData> CaptureAssets()
	{
		RequireThread();
		var descriptions = new Dictionary<AssetReference, ScriptableAssetData>();
		if (loadedGraphs.TryGetValue(currentSession, out var graphs))
			foreach (var graph in graphs.Values)
				foreach (var data in graph.Capture())
				{
					if (descriptions.TryGetValue(data.Reference, out var previous) &&
					    (previous.TypeId != data.TypeId || previous.TypeVersion != data.TypeVersion ||
					     !System.Text.Json.Nodes.JsonNode.DeepEquals(System.Text.Json.Nodes.JsonNode.Parse(previous.Payload),
					         System.Text.Json.Nodes.JsonNode.Parse(data.Payload))))
						throw new InvalidOperationException("Loaded asset graphs disagree about a shared persistent identity.");
					descriptions[data.Reference] = data;
				}
		if (persistentAssets.TryGetValue(currentSession, out var assets))
			foreach (var entry in assets)
				if (!descriptions.ContainsKey(entry.Key))
				{
					ScriptIdentity identity = BehaviourRegistry.IdentityFor(entry.Value.GetType());
					descriptions.Add(entry.Key, new ScriptableAssetData(entry.Key, identity.Id, identity.Version,
						ScriptFieldSchema.Serialize(entry.Value)));
				}
		return new List<ScriptableAssetData>(descriptions.Values);
	}

	internal static void RestoreAssets(IReadOnlyList<ScriptableAssetData> descriptions)
	{
		RequireThread();
		if (descriptions.Count == 0) return;
		if (persistentAssets.TryGetValue(currentSession, out var assets) && assets.Count != 0)
			throw new InvalidOperationException("Asset reload needs a fresh candidate session.");
		ScriptableAssetGraph graph = ScriptableAssetGraph.Load(descriptions, ResolveAssetType);
		try
		{
			graph.ValidateFields();
			if (!loadedGraphs.TryGetValue(currentSession, out var roots))
			{
				roots = new Dictionary<AssetReference, ScriptableAssetGraph>();
				loadedGraphs.Add(currentSession, roots);
			}
			roots.Add(descriptions[0].Reference, graph);
		}
		catch { graph.Dispose(); throw; }
	}

	internal static Action AcquireAssetLease(ScriptableObject instance, bool owned)
	{
		RequireThread();
		instance.RequireValid();
		AssetReference reference = instance.AssetReference ?? throw new InvalidOperationException("An asset lease requires a persistent identity.");
		ulong session = currentSession;
		if (!assetLeases.TryGetValue(session, out var leases))
		{
			leases = new Dictionary<AssetReference, AssetLease>();
			assetLeases.Add(session, leases);
		}
		if (!leases.TryGetValue(reference, out AssetLease? lease))
		{
			lease = new AssetLease();
			leases.Add(reference, lease);
		}
		checked { ++lease.count; }
		lease.owned |= owned;
		bool released = false;
		return () =>
		{
			RequireThread();
			if (released) return;
			released = true;
			if (!assetLeases.TryGetValue(session, out var active) ||
			    !active.TryGetValue(reference, out var current) || !ReferenceEquals(current, lease)) return;
			if (--lease.count != 0) return;
			active.Remove(reference);
			if (active.Count == 0) assetLeases.Remove(session);
			if (lease.owned) instance.Release();
		};
	}

	internal static IReadOnlyList<Exception> ClearSession(ulong session)
	{
		RequireThread();
		var errors = new List<Exception>();
		if (!sessions.TryGetValue(session, out Dictionary<ulong, ScriptableObject>? objects))
		{
			loadedGraphs.Remove(session);
			assetLeases.Remove(session);
			persistentAssets.Remove(session);
			return errors;
		}
		closingSessions.Add(session);
		try
		{
			if (loadedGraphs.Remove(session, out var graphs))
				foreach (ScriptableAssetGraph graph in graphs.Values)
					try { graph.Dispose(); } catch (Exception error) { errors.Add(error); }
			foreach (ScriptableObject instance in new List<ScriptableObject>(objects.Values))
			{
				try { instance.Release(); } catch (Exception error) { errors.Add(error); }
			}
		}
		finally
		{
			sessions.Remove(session);
			persistentAssets.Remove(session);
			assetLeases.Remove(session);
			closingSessions.Remove(session);
		}
		return errors;
	}
}
