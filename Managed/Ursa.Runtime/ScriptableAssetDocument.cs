using System;
using System.Collections.Generic;
using System.Linq;

namespace Ursa;

internal sealed record MissingScriptableAsset(ScriptableAssetData Data, string Diagnostic);

// Editor/reload state keeps raw descriptions even when no executable CLR type is available.
internal sealed class ScriptableAssetDocument : IDisposable
{
	private ScriptableAssetData[] descriptions;
	private ScriptableAssetGraph? graph;
	private MissingScriptableAsset[] missing = Array.Empty<MissingScriptableAsset>();
	private bool disposed;

	internal ScriptableAssetDocument(IReadOnlyList<ScriptableAssetData> data)
	{
		ArgumentNullException.ThrowIfNull(data);
		if (data.Count > 4096) throw new InvalidOperationException("Asset document exceeds its asset budget.");
		descriptions = data.ToArray();
	}

	internal IReadOnlyList<ScriptableAssetData> RawDescriptions => descriptions;
	internal IReadOnlyList<MissingScriptableAsset> MissingAssets => missing;
	internal string Diagnostic { get; private set; } = "Asset types have not been resolved.";
	internal string CleanupDiagnostic { get; private set; } = string.Empty;
	internal bool CanPublish => !disposed && graph != null && Diagnostic.Length == 0;

	internal bool TryReload(Func<ScriptableAssetData, Type?> resolveType)
	{
		RequireOpen();
		// Include unsaved supported fields and unknown data in a candidate reload.
		try
		{
			var snapshot = graph == null ? descriptions : descriptions.Select(data =>
				data with { Payload = graph.Serialize(data.Reference) }).ToArray();
			return TryReload(snapshot, resolveType);
		}
		catch (Exception error)
		{
			Diagnostic = error.ToString();
			return false;
		}
	}

	internal bool TryReload(IReadOnlyList<ScriptableAssetData> candidateData, Func<ScriptableAssetData, Type?> resolveType)
	{
		RequireOpen();
		ArgumentNullException.ThrowIfNull(candidateData);
		ArgumentNullException.ThrowIfNull(resolveType);
		if (candidateData.Count > 4096)
		{
			Diagnostic = "Asset document exceeds its asset budget.";
			return false;
		}
		ScriptableAssetData[] candidateDescriptions = candidateData.ToArray();
		var unresolved = new List<MissingScriptableAsset>();
		var types = new Dictionary<AssetReference, Type>();
		ScriptableAssetGraph? candidate = null;
		try
		{
			foreach (ScriptableAssetData data in candidateDescriptions)
			{
				Type? type = resolveType(data);
				if (type == null)
				{
					unresolved.Add(new MissingScriptableAsset(data,
						$"Missing asset type '{data.TypeId}' at {data.Reference.Guid}:{data.Reference.LocalFileId}."));
					continue;
				}
				types.Add(data.Reference, type);
			}
			if (unresolved.Count != 0)
			{
				missing = unresolved.ToArray();
				Diagnostic = string.Join(Environment.NewLine, missing.Select(value => value.Diagnostic));
				return false;
			}
			candidate = ScriptableAssetGraph.LoadCandidate(candidateDescriptions, data => types[data.Reference]);
		}
		catch (Exception error)
		{
			missing = Array.Empty<MissingScriptableAsset>();
			Diagnostic = error.ToString();
			return false;
		}
		ScriptableAssetGraph? previous = graph;
		graph = candidate;
		descriptions = candidateDescriptions;
		missing = Array.Empty<MissingScriptableAsset>();
		Diagnostic = string.Empty;
		CleanupDiagnostic = string.Empty;
		try { previous?.Dispose(); }
		catch (Exception error) { CleanupDiagnostic = error.ToString(); }
		return true;
	}

	internal ScriptableObject Get(AssetReference reference)
	{
		RequireOpen();
		return graph?.Get(reference) ?? throw new InvalidOperationException(Diagnostic);
	}

	internal string Serialize(AssetReference reference)
	{
		RequireOpen();
		if (!CanPublish) throw new InvalidOperationException("Cannot save or publish an unresolved asset: " + Diagnostic);
		return graph!.Serialize(reference);
	}

	private void RequireOpen()
	{
		ScriptableObjectRegistry.RequireThread();
		if (disposed) throw new ObjectDisposedException(nameof(ScriptableAssetDocument));
	}

	public void Dispose()
	{
		ScriptableObjectRegistry.RequireThread();
		if (disposed) return;
		disposed = true;
		try { graph?.Dispose(); }
		finally
		{
			graph = null;
			descriptions = Array.Empty<ScriptableAssetData>();
			missing = Array.Empty<MissingScriptableAsset>();
		}
	}
}
