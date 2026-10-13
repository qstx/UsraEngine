using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using Ursa;

internal static class ScriptableAssetFieldTests
{
	[UrsaScriptType("test.serializable-asset", Version = 2)]
	private sealed class Settings : ScriptableObject
	{
		public float exposure = 2.0f;
		public Vector2 size = new(8, 4);
		public Vector4 tint = new(1, 2, 3, 4);
		public List<int> values = new() { 2, 4 };
		public Settings? other;
		public Settings?[] shared = Array.Empty<Settings?>();
		public GameObject? sceneObject = null;
		[SerializeField, FormerlySerializedAs("oldName")] private string title = "Default";

		internal string Title => title;
	}

	internal static void Run()
	{
		ScriptableObjectRegistry.SelectSession(903);
		try
		{
			using var first = ScriptableObject.CreateInstance<Settings>();
			using var second = ScriptableObject.CreateInstance<Settings>();
			first.BindAsset(new AssetReference("33333333-3333-4333-8333-333333333333", 1));
			second.BindAsset(new AssetReference("44444444-4444-4444-8444-444444444444", 1));
			first.other = second;
			first.shared = new[] { second, second, null };
			second.other = first;
			string serialized = ScriptFieldSchema.Serialize(first);
			first.exposure = 9;
			first.other = null;
			first.shared = Array.Empty<Settings?>();
			ScriptFieldSchema.Deserialize(first, serialized);
			ScriptFieldSchema.ResolvePendingReferences(first);
			Expect(first.exposure == 2 && first.size.X == 8 && first.tint.W == 4 && first.values.SequenceEqual(new[] {2, 4}) &&
				ReferenceEquals(first.other, second) && ReferenceEquals(first.shared[0], first.shared[1]) &&
				first.shared[2] == null && ReferenceEquals(second.other, first), "Shared or cyclic field reference changed.");
			JsonObject payload = JsonNode.Parse(serialized)!.AsObject();
			payload["Fields"]!["oldName"] = payload["Fields"]!["title"]!.DeepClone();
			payload["Fields"]!.AsObject().Remove("title");
			ScriptFieldSchema.Deserialize(first, payload.ToJsonString());
			ScriptFieldSchema.ResolvePendingReferences(first);
			Expect(first.Title == "Default", "Former field name failed.");
			using var unsaved = ScriptableObject.CreateInstance<Settings>();
			first.other = unsaved;
			Reject(() => ScriptFieldSchema.Serialize(first));
			first.other = second;
			payload = JsonNode.Parse(serialized)!.AsObject();
			payload["Fields"]!["sceneObject"] = JsonNode.Parse(
				"""{"$ursaReference":{"Kind":"GameObject","Id":"scene-object"}}""");
			Reject(() => ScriptFieldSchema.Deserialize(first, payload.ToJsonString()));
			payload = JsonNode.Parse(serialized)!.AsObject();
			payload["Version"] = 99;
			Reject(() => ScriptFieldSchema.Deserialize(first, payload.ToJsonString()));
			Expect(ScriptFieldSchema.Describe(typeof(Settings)).Count == 8, "Base infrastructure fields were serialized.");
		}
		finally
		{
			ScriptableObjectRegistry.ClearSession(903);
			ScriptableObjectRegistry.SelectSession(0);
		}
	}

	private static void Expect(bool value, string message)
	{
		if (!value) throw new InvalidOperationException(message);
	}

	private static void Reject(Action action)
	{
		try { action(); } catch (InvalidOperationException) { return; }
		throw new InvalidOperationException("Invalid persistent field was accepted.");
	}
}
