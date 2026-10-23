using System;
using System.Text.Json.Nodes;
using Ursa;

internal static class ScriptableAssetRecoveryTests
{
	private sealed class Before : ScriptableObject
	{
		public int amount = 5;
		public Before? other = null;
	}

	private sealed class Renamed : ScriptableObject
	{
		[FormerlySerializedAs("amount")] public int count = 1;
		public Renamed? other = null;
	}

	private sealed class Incompatible : ScriptableObject
	{
		public string amount = "original";
	}

	private sealed class Throwing : ScriptableObject
	{
		public int amount = 1;
		protected override void OnEnable() => throw new InvalidOperationException("candidate activation failed");
	}

	internal static void Run()
	{
		ScriptableObjectRegistry.SelectSession(905);
		try
		{
			var first = new AssetReference("33333333-3333-4333-8333-111111111111", 1);
			var second = new AssetReference("44444444-4444-4444-8444-111111111111", 1);
			string payload;
			using (var original = ScriptableObject.CreateInstance<Before>())
			{
				JsonObject data = JsonNode.Parse(ScriptFieldSchema.Serialize(original))!.AsObject();
				data["Fields"]!["future"] = JsonNode.Parse("""{"nested":[1,"keep",true]}""");
				data["Fields"]!["other"] = Reference(second);
				payload = data.ToJsonString();
			}
			JsonObject back = JsonNode.Parse(payload)!.AsObject();
			back["Fields"]!["other"] = Reference(first);
			var descriptions = new[]
			{
				new ScriptableAssetData(first, "test.recoverable-settings", 1, payload),
				new ScriptableAssetData(second, "test.recoverable-settings", 1, back.ToJsonString())
			};
			using var document = new ScriptableAssetDocument(descriptions);
			Expect(!document.TryReload(_ => null) && !document.CanPublish && document.MissingAssets.Count == 2 &&
				document.MissingAssets[0].Data.Payload == payload, "Missing type placeholder lost source data.");
			Reject(() => document.Serialize(first));
			Expect(document.TryReload(_ => typeof(Before)), "The restored asset type did not load.");
			var active = (Before)document.Get(first);
			Expect(ReferenceEquals(active.other!.other, active), "Candidate cyclic references did not resolve locally.");
			active.amount = 17;
			_ = document.Serialize(first);
			Expect(!document.TryReload(_ => typeof(Incompatible)) && active.IsValid && active.amount == 17 &&
				document.Get(first) == active && document.Diagnostic.Contains("amount", StringComparison.Ordinal),
				"An incompatible candidate changed the active asset or lost its diagnostic.");
			Reject(() => document.Serialize(first));
			Expect(document.RawDescriptions[0].Payload == payload, "Failed reload changed the persistent source description.");
			Expect(!document.TryReload(_ => typeof(Throwing)) && active.IsValid && active.amount == 17,
				"Callback failure destroyed the active asset.");
			Expect(!document.TryReload(_ => null) && active.IsValid && !document.CanPublish,
				"Missing candidate type destroyed the current editing environment.");
			Expect(document.TryReload(_ => typeof(Renamed)), "Compatible type/field rename did not recover.");
			var renamed = (Renamed)document.Get(first);
			Expect(renamed.count == 17 && !active.IsValid && ReferenceEquals(renamed.other!.other, renamed),
				"Compatible reload lost unsaved fields, cycles or old instance invalidation.");
			JsonObject saved = JsonNode.Parse(document.Serialize(first))!.AsObject();
			Expect(saved["Fields"]!["future"]!["nested"]![1]!.GetValue<string>() == "keep" &&
				saved["Fields"]!["count"]!.GetValue<int>() == 17 && !saved["Fields"]!.AsObject().ContainsKey("amount"),
				"Compatible save lost unknown data or retained a stale historical field alias.");
			var reopenedDescriptions = new[]
			{
				descriptions[0] with { Payload = document.Serialize(first) },
				descriptions[1] with { Payload = document.Serialize(second) }
			};
			using var reopened = new ScriptableAssetDocument(reopenedDescriptions);
			Expect(reopened.TryReload(_ => typeof(Renamed)) && ((Renamed)reopened.Get(first)).count == 17,
				"Saved recovery data did not reopen.");
		}
		finally
		{
			ScriptableObjectRegistry.ClearSession(905);
			ScriptableObjectRegistry.SelectSession(0);
		}
	}

	private static JsonNode Reference(AssetReference reference) => JsonNode.Parse(
		$"{{\"$ursaReference\":{{\"Kind\":\"ScriptableObject\",\"Id\":\"\",\"Resource\":{{\"guid\":\"{reference.Guid}\",\"localFileId\":1}}}}}}")!;

	private static void Expect(bool condition, string message)
	{
		if (!condition) throw new InvalidOperationException(message);
	}

	private static void Reject(Action action)
	{
		try { action(); } catch (InvalidOperationException) { return; }
		throw new InvalidOperationException("An unresolved asset was saved.");
	}
}
