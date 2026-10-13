using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;
using Ursa;

internal static class ShaderFieldTests
{
	[UrsaScriptType("demo.render.settings", Version = 1)]
	private sealed class Settings : ScriptableObject
	{
		public float exposure = 1.0f;
		public Shader? shader;
		public ComputeShader? compute;
		public Texture? lookup;
		public Settings? shared;
		public Shader?[] shaders = Array.Empty<Shader?>();
		public List<ComputeShader?> kernels = new();
	}

	[UrsaScriptType("test.shader-behaviour")]
	private sealed class Script : Behaviour
	{
		public Shader? shader = null;
		public List<ComputeShader?> kernels = new();
	}

	internal static void Run()
	{
		var graphics = new AssetReference("33333333-2222-4333-8333-333333333333", 7);
		var compute = new AssetReference("44444444-2222-4444-8444-444444444444", 2);
		var texture = new AssetReference("11111111-1111-4111-8111-111111111111", 0);
		var asset = new AssetReference("66666666-2222-4666-8666-444444444444", 1);
		using var settings = ScriptableObject.CreateInstance<Settings>();
		settings.BindAsset(asset);
		settings.shader = Shader.FromReference(graphics);
		settings.compute = ComputeShader.FromReference(compute);
		settings.lookup = Texture.Load(texture);
		settings.shared = settings;
		settings.shaders = new[] { settings.shader, null, settings.shader };
		settings.kernels.AddRange(new[] { settings.compute, null });
		string payload = ScriptFieldSchema.Serialize(settings);
		settings.shader = null;
		settings.compute = null;
		settings.shaders = Array.Empty<Shader?>();
		settings.kernels.Clear();
		ScriptFieldSchema.Deserialize(settings, payload);
		Expect(settings.shader?.Reference.Equals(graphics) == true && settings.compute?.Reference.Equals(compute) == true &&
			settings.exposure == 1.0f && ReferenceEquals(settings.shared, settings) &&
			settings.shaders.Length == 3 && settings.shaders[1] == null &&
			settings.shaders[2]?.Reference.Equals(graphics) == true && settings.kernels.Count == 2 &&
			settings.kernels[0]?.Reference.Equals(compute) == true && settings.kernels[1] == null,
			"The documented asset type or its typed references and collections did not round-trip.");
		Expect(ScriptFieldSchema.Describe(typeof(Settings)).All(field => field.IsSupported), "Shader fields were unsupported.");
		string savedPayload = ScriptFieldSchema.Serialize(settings);
		using (var created = ScriptableAssetGraph.LoadCandidate(
			new[] { new ScriptableAssetData(asset, "demo.render.settings", 1, savedPayload) }, _ => typeof(Settings)))
		{
			var createdSettings = (Settings)created.Get(asset);
			Expect(createdSettings.exposure == 1.0f && ReferenceEquals(createdSettings.shared, createdSettings) &&
				createdSettings.shader?.Reference.Equals(graphics) == true,
				"The documented settings asset could not be created with its defaults and references.");
			savedPayload = created.Serialize(asset);
		}
		using (var reopened = ScriptableAssetGraph.LoadCandidate(
			new[] { new ScriptableAssetData(asset, "demo.render.settings", 1, savedPayload) }, _ => typeof(Settings)))
			Expect(ReferenceEquals(((Settings)reopened.Get(asset)).shared, reopened.Get(asset)),
				"The documented settings asset could not be reopened.");
		var script = new Script { shader = settings.shader, kernels = new() { settings.compute, null } };
		string scriptPayload = ScriptFieldSchema.Serialize(script);
		script.shader = null;
		script.kernels.Clear();
		ScriptFieldSchema.Deserialize(script, scriptPayload);
		Expect(script.shader?.Reference.Equals(graphics) == true && script.kernels.Count == 2,
			"Behaviour did not use the same Shader field service.");
		JsonObject invalid = JsonNode.Parse(payload)!.AsObject();
		invalid["Fields"]!["exposure"] = 99;
		invalid["Fields"]!["shader"]!["$ursaReference"]!["Kind"] = "ComputeShader";
		Reject(() => ScriptFieldSchema.Deserialize(settings, invalid.ToJsonString()));
		Expect(settings.exposure == 1.0f && settings.shader?.Reference.Equals(graphics) == true,
			"An incompatible reference changed fields before validation.");
		invalid = JsonNode.Parse(payload)!.AsObject();
		invalid["Fields"]!["shaders"]![2]!["$ursaReference"]!["Kind"] = "Texture";
		Reject(() => ScriptFieldSchema.Deserialize(settings, invalid.ToJsonString()));
		Reject(() => Shader.FromReference(default));
		Reject(() => ComputeShader.FromReference(new AssetReference(Guid.Empty.ToString("D"))));
	}

	private static void Expect(bool condition, string message)
	{
		if (!condition) throw new InvalidOperationException(message);
	}

	private static void Reject(Action action)
	{
		try { action(); } catch (Exception error) when (error is InvalidOperationException or ArgumentException) { return; }
		throw new InvalidOperationException("An invalid Shader reference was accepted.");
	}
}
