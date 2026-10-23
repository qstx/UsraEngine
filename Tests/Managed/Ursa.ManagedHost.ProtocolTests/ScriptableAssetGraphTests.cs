using System;
using System.Text.Json;
using Ursa;

internal static class ScriptableAssetGraphTests
{
	private sealed class Settings : ScriptableObject
	{
		public int amount;
		public Settings? other = null;
		[NonSerialized] public bool enabledWithResolvedGraph;

		protected override void OnEnable()
		{
			enabledWithResolvedGraph = other != null && other.other == this && other.amount != 0;
		}
	}

	internal static void Run()
	{
		ScriptableObjectRegistry.SelectSession(904);
		var first = new AssetReference("33333333-1111-4333-8333-333333333333", 1);
		var second = new AssetReference("44444444-1111-4444-8444-444444444444", 1);
		string Payload(AssetReference other, int amount, uint version = 1) => JsonSerializer.Serialize(new
		{
			FormatVersion = 1, ScriptType = "test.graph-settings", Version = version,
			Fields = new
			{
				amount, other = JsonSerializer.SerializeToElement(new System.Collections.Generic.Dictionary<string, object>
				{
					["$ursaReference"] = new { Kind = "ScriptableObject", Id = "", Resource = new { guid = other.Guid, localFileId = other.LocalFileId } }
				}),
				future = new { nested = "unknown data" }
			}
		});
		try
		{
			var descriptions = new[]
			{
				new ScriptableAssetData(first, "test.graph-settings", 1, Payload(second, 2)),
				new ScriptableAssetData(second, "test.graph-settings", 1, Payload(first, 4))
			};
			using var graph = ScriptableAssetGraph.Load(descriptions, _ => typeof(Settings));
			var a = (Settings)graph.Get(first);
			var b = (Settings)graph.Get(second);
			if (!ReferenceEquals(a.other, b) || !ReferenceEquals(b.other, a) ||
			    !a.enabledWithResolvedGraph || !b.enabledWithResolvedGraph)
				throw new Exception("Assets activated before the complete field/reference graph was restored.");
			a.amount = 7;
			using var serialized = JsonDocument.Parse(graph.Serialize(first));
			if (serialized.RootElement.GetProperty("Fields").GetProperty("amount").GetInt32() != 7 ||
			    serialized.RootElement.GetProperty("Fields").GetProperty("future").GetProperty("nested").GetString() != "unknown data")
				throw new Exception("Asset editing lost a supported or unknown field.");
			using var cached = ScriptableAssetGraph.Load(descriptions, _ => typeof(Settings));
			if (!ReferenceEquals(cached.Get(first), a) || a.amount != 7)
				throw new Exception("Repeated graph loading duplicated or overwrote a cached asset.");
			var missing = new AssetReference("55555555-1111-4555-8555-555555555555", 1);
			try
			{
				ScriptableAssetGraph.Load(new[] { new ScriptableAssetData(missing, "test.graph-settings", 1, Payload(first, 1, 99)) },
					_ => typeof(Settings));
				throw new Exception("Malformed graph was published.");
			}
			catch (InvalidOperationException)
			{
				if (ScriptableObjectRegistry.FindAsset(missing) != null || !a.IsValid)
					throw new Exception("Failed candidate retained a partial asset or destroyed the active graph.");
			}
			graph.Dispose();
			if (!a.IsValid || !ReferenceEquals(cached.Get(second), b))
				throw new Exception("Unloading one graph destroyed another graph's shared assets.");
			cached.Dispose();
			if (a.IsValid || b.IsValid || ScriptableObjectRegistry.FindAsset(first) != null)
				throw new Exception("The final graph lease did not release its assets.");
			using var external = ScriptableObject.CreateInstance<Settings>();
			external.BindAsset(first);
			using (var borrowed = ScriptableAssetGraph.Load(new[] { descriptions[0] }, _ => typeof(Settings)))
				if (!ReferenceEquals(external, borrowed.Get(first))) throw new Exception("Existing asset was not reused.");
			if (!external.IsValid) throw new Exception("A graph destroyed an asset owned by its caller.");
		}
		finally
		{
			ScriptableObjectRegistry.ClearSession(904);
			ScriptableObjectRegistry.SelectSession(0);
		}
	}
}
