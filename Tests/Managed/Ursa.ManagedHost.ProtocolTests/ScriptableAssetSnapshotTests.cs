using System;
using System.Text.Json;
using Ursa;

internal static class ScriptableAssetSnapshotTests
{
	[UrsaScriptType("test.snapshot-fields")]
	private sealed class Settings : ScriptableObject
	{
		public int amount;
		public Settings? self = null;
	}

	internal static void Run()
	{
		var previousAssemblies = BehaviourRegistry.assemblyResolver;
		var previousIdentity = BehaviourRegistry.identityResolver;
		var previousLoader = ScriptableObjectRegistry.graphLoader;
		const ulong source = 910, candidate = 911;
		var reference = new AssetReference("23727710-9201-41ab-94da-1a4c8cb2e119", 1);
		try
		{
			BehaviourRegistry.assemblyResolver = () => new[] { typeof(Settings).Assembly };
			BehaviourRegistry.identityResolver = null;
			ScriptableObjectRegistry.SelectSession(source);
			string payload = JsonSerializer.Serialize(new
			{
				FormatVersion = 1, ScriptType = "test.snapshot-fields", Version = 1,
				Fields = new
				{
					amount = 2, future = new { preserved = "unknown" },
					self = new System.Collections.Generic.Dictionary<string, object>
					{
						["$ursaReference"] = new { Kind = "ScriptableObject", Id = "", Resource = new { guid = reference.Guid, localFileId = 1 } }
					}
				}
			});
			ScriptableObjectRegistry.graphLoader = _ => new[] { new ScriptableAssetData(reference, "test.snapshot-fields", 1, payload) };
			Settings original = ScriptableObject.Load<Settings>(reference);
			original.amount = 17;
			var snapshot = ScriptableObjectRegistry.CaptureAssets();
			using var captured = JsonDocument.Parse(snapshot[0].Payload);
			if (captured.RootElement.GetProperty("Fields").GetProperty("future").GetProperty("preserved").GetString() != "unknown")
				throw new InvalidOperationException("Session snapshot lost unknown fields.");
			ScriptableObjectRegistry.SelectSession(candidate);
			ScriptableObjectRegistry.RestoreAssets(snapshot);
			var restored = (Settings)ScriptableObjectRegistry.ResolveAsset(reference, typeof(Settings));
			if (restored.amount != 17 || !ReferenceEquals(restored.self, restored) || ReferenceEquals(restored, original))
				throw new InvalidOperationException("Session snapshot lost fields, cycles or independent candidate identity.");
			ScriptableObjectRegistry.SelectSession(source);
			ScriptableObjectRegistry.ClearSession(source);
			ScriptableObjectRegistry.SelectSession(candidate);
			if (original.IsValid || !restored.IsValid || restored.amount != 17)
				throw new InvalidOperationException("Source close retained an old asset or invalidated candidate fields.");
		}
		finally
		{
			ScriptableObjectRegistry.SelectSession(source);
			ScriptableObjectRegistry.ClearSession(source);
			ScriptableObjectRegistry.SelectSession(candidate);
			ScriptableObjectRegistry.ClearSession(candidate);
			ScriptableObjectRegistry.SelectSession(0);
			BehaviourRegistry.assemblyResolver = previousAssemblies;
			BehaviourRegistry.identityResolver = previousIdentity;
			ScriptableObjectRegistry.graphLoader = previousLoader;
		}
	}
}
