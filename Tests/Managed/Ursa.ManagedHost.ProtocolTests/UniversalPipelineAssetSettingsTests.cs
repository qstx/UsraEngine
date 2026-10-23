using System;
using System.Linq;
using System.Text.Json;
using Ursa;
using Ursa.Rendering;

internal static class UniversalPipelineAssetSettingsTests
{
	internal static void Run()
	{
		var previousIdentityResolver = BehaviourRegistry.identityResolver;
		BehaviourRegistry.identityResolver = type => type == typeof(UniversalRenderPipelineAsset)
			? new ScriptIdentity("ursa.rendering.universal-pipeline", 2)
			: throw new InvalidOperationException($"No test identity configured for {type.FullName}.");
		ScriptableObjectRegistry.SelectSession(914);
		try
		{
			using var asset = ScriptableObject.CreateInstance<UniversalRenderPipelineAsset>();
			Expect(asset.ExposureEV == 0 && asset.ToneMapping == ToneMappingMode.Reinhard &&
			       !asset.BloomEnabled && !asset.FxaaEnabled && asset.BloomThreshold == 1 &&
			       asset.BloomIntensity == 0.5f && asset.BloomScatter == 0.7f,
			       "Universal pipeline postprocessing defaults changed.");

			asset.ExposureEV = 1.25f;
			asset.ToneMapping = ToneMappingMode.None;
			asset.BloomEnabled = true;
			asset.BloomThreshold = 2.5f;
			asset.BloomIntensity = 0.75f;
			asset.BloomScatter = 0.3f;
			asset.FxaaEnabled = true;
			asset.MainLightShadowCascadeCount = 4;
			asset.MainLightShadowSplitFractions = new[] { 0.1f, 0.3f, 0.6f };
			asset.MainLightShadowAtlasHeight = 2048;
			string serialized = ScriptFieldSchema.Serialize(asset);
			using (JsonDocument document = JsonDocument.Parse(serialized))
			{
				JsonElement fields = document.RootElement.GetProperty("Fields");
				Expect(fields.TryGetProperty("ExposureEV", out _) && fields.TryGetProperty("ToneMapping", out _) &&
				       fields.TryGetProperty("BloomEnabled", out _) && fields.TryGetProperty("BloomScatter", out _),
				       "Universal pipeline postprocessing settings were not included in the serialized asset payload.");
			}

			asset.ExposureEV = 0;
			asset.ToneMapping = ToneMappingMode.Reinhard;
			asset.BloomEnabled = false;
			asset.BloomThreshold = 1;
			asset.BloomIntensity = 0.5f;
			asset.BloomScatter = 0.7f;
			asset.FxaaEnabled = false;
			asset.MainLightShadowCascadeCount = 2;
			asset.MainLightShadowSplitFractions = new[] { 0.25f };
			asset.MainLightShadowAtlasHeight = 1024;
			ScriptFieldSchema.Deserialize(asset, serialized);
			Expect(asset.ExposureEV == 1.25f && asset.ToneMapping == ToneMappingMode.None && asset.BloomEnabled &&
			       asset.BloomThreshold == 2.5f && asset.BloomIntensity == 0.75f && asset.BloomScatter == 0.3f &&
			       asset.FxaaEnabled && asset.MainLightShadowCascadeCount == 4 &&
			       asset.MainLightShadowSplitFractions.SequenceEqual(new[] { 0.1f, 0.3f, 0.6f }) &&
			       asset.MainLightShadowAtlasHeight == 2048,
			       "Universal pipeline settings did not round-trip through ScriptableObject serialization.");
			asset.ValidateConfiguration();

			Reject(() => { asset.ExposureEV = float.NaN; asset.ValidateConfiguration(); }, "Non-finite EV was accepted.");
			asset.ExposureEV = 0;
			Reject(() => { asset.ExposureEV = 10.01f; asset.ValidateConfiguration(); }, "Out-of-range EV was accepted.");
			asset.ExposureEV = 0;
			Reject(() => { asset.ToneMapping = (ToneMappingMode)99; asset.ValidateConfiguration(); },
				"Unknown tone mapping mode was accepted.");
			asset.ToneMapping = ToneMappingMode.Reinhard;
			Reject(() => { asset.BloomThreshold = float.PositiveInfinity; asset.ValidateConfiguration(); },
				"Non-finite Bloom threshold was accepted.");
			asset.BloomThreshold = 1;
			Reject(() => { asset.BloomIntensity = -0.01f; asset.ValidateConfiguration(); },
				"Negative Bloom intensity was accepted.");
			asset.BloomIntensity = 0.5f;
			Reject(() => { asset.BloomScatter = 1.01f; asset.ValidateConfiguration(); },
				"Bloom scatter outside [0, 1] was accepted.");
			asset.BloomScatter = 0.7f;
			asset.BloomEnabled = false;
			asset.FxaaEnabled = false;
			Expect(!asset.BloomEnabled && !asset.FxaaEnabled,
			       "Disabled postprocessing defaults were not retained on the Universal pipeline asset.");
		}
		finally
		{
			ScriptableObjectRegistry.ClearSession(914);
			ScriptableObjectRegistry.SelectSession(0);
			BehaviourRegistry.identityResolver = previousIdentityResolver;
		}
	}

	private static void Reject(Action action, string message)
	{
		try { action(); }
		catch (ArgumentException) { return; }
		throw new Exception(message);
	}

	private static void Expect(bool value, string message)
	{
		if (!value) throw new Exception(message);
	}
}
