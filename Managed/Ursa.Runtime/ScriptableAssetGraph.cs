using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Ursa;

internal sealed record ScriptableAssetData(AssetReference Reference, string TypeId, uint TypeVersion, string Payload);

internal sealed class ScriptableAssetGraph : IDisposable
{
	private readonly Dictionary<AssetReference, ScriptableObject> instances = new();
	private readonly Dictionary<AssetReference, ScriptableAssetData> descriptions = new();
	private readonly List<ScriptableObject> owned = new();
	private readonly List<Action> releaseLeases = new();
	private bool disposed;
	internal AssetReference Root { get; private set; }
	internal IEnumerable<ScriptableObject> Instances => instances.Values;

	internal static ScriptableAssetGraph Load(IReadOnlyList<ScriptableAssetData> data, Func<ScriptableAssetData, Type> resolveType)
	{
		return Load(data, resolveType, isolated: false);
	}

	internal static ScriptableAssetGraph LoadCandidate(IReadOnlyList<ScriptableAssetData> data, Func<ScriptableAssetData, Type> resolveType)
	{
		return Load(data, resolveType, isolated: true);
	}

	private static ScriptableAssetGraph Load(IReadOnlyList<ScriptableAssetData> data, Func<ScriptableAssetData, Type> resolveType, bool isolated)
	{
		ArgumentNullException.ThrowIfNull(data);
		ArgumentNullException.ThrowIfNull(resolveType);
		ScriptableObjectRegistry.RequireThread();
		if (data.Count > 4096) throw new InvalidOperationException("ScriptableObject graph exceeds its asset budget.");
		var graph = new ScriptableAssetGraph();
		if (data.Count != 0) graph.Root = data[0].Reference;
		try
		{
			// Establish every asset identity before restoring references, including cycles.
			foreach (ScriptableAssetData description in data)
			{
				if (string.IsNullOrEmpty(description.Reference.Guid) || description.Reference.LocalFileId == 0 ||
				    string.IsNullOrWhiteSpace(description.TypeId) || description.TypeVersion == 0 ||
				    !graph.descriptions.TryAdd(description.Reference, description))
					throw new InvalidOperationException("ScriptableObject graph has an empty or duplicate asset identity.");
				Type type = resolveType(description);
				ScriptIdentity identity = BehaviourRegistry.IdentityFor(type);
				if (identity.Id != description.TypeId || identity.Version != description.TypeVersion)
					throw new InvalidOperationException("ScriptableObject graph type identity/version differs from its catalog.");
				ScriptableObject? instance = isolated ? null : ScriptableObjectRegistry.FindAsset(description.Reference);
				if (instance != null && !type.IsInstanceOfType(instance))
					throw new InvalidOperationException("Loaded ScriptableObject has a different persistent type.");
				if (instance == null)
				{
					instance = ScriptableObject.CreateForLoad(type);
					try
					{
						instance.BindAsset(description.Reference, register: !isolated);
						graph.releaseLeases.Add(isolated ? instance.Release : ScriptableObjectRegistry.AcquireAssetLease(instance, owned: true));
						graph.owned.Add(instance);
					}
					catch
					{
						try { instance.Release(); } catch { }
						throw;
					}
				}
				else graph.releaseLeases.Add(ScriptableObjectRegistry.AcquireAssetLease(instance, owned: false));
				graph.instances.Add(description.Reference, instance);
			}
			ScriptableObject Resolve(AssetReference reference, Type expectedType)
			{
				if (!graph.instances.TryGetValue(reference, out ScriptableObject? result))
				{
					if (!isolated) return ScriptableObjectRegistry.ResolveAsset(reference, expectedType);
					throw new InvalidOperationException($"Candidate asset dependency is missing: {reference.Guid}:{reference.LocalFileId}.");
				}
				if (!expectedType.IsInstanceOfType(result)) throw new InvalidOperationException("Asset dependency type differs from its field.");
				return result;
			}
			foreach (ScriptableObject instance in graph.owned)
				ScriptFieldSchema.Deserialize(instance, graph.descriptions[instance.AssetReference!.Value].Payload, Resolve);
			foreach (ScriptableObject instance in graph.owned)
				ScriptFieldSchema.ResolvePendingReferences(instance);
			foreach (ScriptableObject instance in graph.owned) instance.Activate();
			return graph;
		}
		catch
		{
			try { graph.Dispose(); } catch { /* Keep the failed candidate's original diagnostic. */ }
			throw;
		}
	}

	internal ScriptableObject Get(AssetReference reference)
	{
		if (disposed) throw new ObjectDisposedException(nameof(ScriptableAssetGraph));
		ScriptableObjectRegistry.RequireThread();
		if (!instances.TryGetValue(reference, out ScriptableObject? instance))
			throw new InvalidOperationException("Asset is outside this ScriptableObject graph.");
		instance.RequireValid();
		return instance;
	}

	internal string Serialize(AssetReference reference)
	{
		ScriptableObject instance = Get(reference);
		JsonObject original = JsonNode.Parse(descriptions[reference].Payload)!.AsObject();
		JsonObject current = JsonNode.Parse(ScriptFieldSchema.Serialize(instance))!.AsObject();
		// Preserve fields unknown to the current assembly while replacing its supported fields.
		JsonObject fields = original["Fields"]!.AsObject();
		JsonObject currentFields = current["Fields"]!.AsObject();
		foreach (ScriptFieldDescriptor descriptor in ScriptFieldSchema.Describe(instance.GetType()))
			foreach (string former in descriptor.FormerNames)
				if (!currentFields.ContainsKey(former))
				{
					fields.Remove(former);
					if (original["FieldTypes"] is JsonObject types) types.Remove(former);
				}
		foreach (var field in currentFields) fields[field.Key] = field.Value?.DeepClone();
		current["Fields"] = fields.DeepClone();
		if (original["FieldTypes"] is JsonObject oldTypes && current["FieldTypes"] is JsonObject newTypes)
			foreach (var field in oldTypes) if (!newTypes.ContainsKey(field.Key)) newTypes[field.Key] = field.Value?.DeepClone();
		return current.ToJsonString();
	}

	internal IReadOnlyList<ScriptableAssetData> Capture()
	{
		var result = new List<ScriptableAssetData>(descriptions.Count);
		foreach (var description in descriptions.Values)
			result.Add(description with { Payload = Serialize(description.Reference) });
		return result;
	}

	internal void ValidateFields()
	{
		foreach (ScriptableObject instance in owned) instance.ValidateFields();
	}

	public void Dispose()
	{
		ScriptableObjectRegistry.RequireThread();
		if (disposed) return;
		disposed = true;
		var errors = new List<Exception>();
		for (int index = releaseLeases.Count - 1; index >= 0; --index)
			try { releaseLeases[index](); } catch (Exception error) { errors.Add(error); }
		releaseLeases.Clear();
		owned.Clear();
		instances.Clear();
		descriptions.Clear();
		if (errors.Count != 0) throw new AggregateException(errors);
	}
}
