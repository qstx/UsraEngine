using System;
using System.Collections.Generic;
using Ursa;

namespace Ursa.Rendering;

[CreateAssetMenu(MenuName = "Rendering/Universal Pipeline", FileName = "UniversalRenderPipeline")]
public sealed class UniversalRenderPipelineAsset : RenderPipelineAsset
{
	public UniversalRendererData? RendererData;
	public bool MainLightShadowsEnabled = true;
	public uint MainLightShadowCascadeCount = 2;
	public float MainLightShadowDistance = 50;
	public float[] MainLightShadowSplitFractions = new[] { 0.25f };
	public uint MainLightShadowAtlasWidth = 2048;
	public uint MainLightShadowAtlasHeight = 1024;
	public float ExposureEV;
	public ToneMappingMode ToneMapping = ToneMappingMode.Reinhard;
	public bool FxaaEnabled;
	public bool BloomEnabled;
	public float BloomThreshold = 1;
	public float BloomIntensity = 0.5f;
	public float BloomScatter = 0.7f;

	protected override RenderPipeline CreatePipeline() => new UniversalRenderPipeline(this);

	internal override ulong GetConfigurationRevision()
	{
		unchecked
		{
			ulong hash = 14695981039346656037UL;
			Mix(ref hash, ValidationRevision);
			Mix(ref hash, RendererData?.InstanceId ?? 0);
			Mix(ref hash, RendererData?.GetConfigurationRevision() ?? 0);
			return hash;
		}
	}

	private static void Mix(ref ulong hash, ulong value)
	{
		unchecked
		{
			for (int index = 0; index < sizeof(ulong); ++index)
			{
				hash ^= (byte)value;
				hash *= 1099511628211UL;
				value >>= 8;
			}
		}
	}

	internal void ValidateConfiguration()
	{
		var shadowSettings = new DirectionalShadowCascadeSettings
		{
			CascadeCount = MainLightShadowCascadeCount,
			ShadowDistance = MainLightShadowDistance,
			SplitFractions = MainLightShadowSplitFractions ?? Array.Empty<float>(),
			AtlasWidth = MainLightShadowAtlasWidth,
			AtlasHeight = MainLightShadowAtlasHeight
		};
		shadowSettings.Validate();
		if (!float.IsFinite(ExposureEV) || ExposureEV < -10 || ExposureEV > 10)
			throw new ArgumentOutOfRangeException(nameof(ExposureEV), "Exposure EV must be finite and in [-10, 10].");
		if (!Enum.IsDefined(ToneMapping)) throw new ArgumentOutOfRangeException(nameof(ToneMapping));
		if (!float.IsFinite(BloomThreshold) || BloomThreshold < 0)
			throw new ArgumentOutOfRangeException(nameof(BloomThreshold), "Bloom threshold must be finite and non-negative.");
		if (!float.IsFinite(BloomIntensity) || BloomIntensity < 0)
			throw new ArgumentOutOfRangeException(nameof(BloomIntensity), "Bloom intensity must be finite and non-negative.");
		if (!float.IsFinite(BloomScatter) || BloomScatter < 0 || BloomScatter > 1)
			throw new ArgumentOutOfRangeException(nameof(BloomScatter), "Bloom scatter must be finite and in [0, 1].");
	}
}

public sealed class UniversalRenderPipeline : RenderPipeline
{
	private readonly RendererFeature[] features;
	private readonly ScriptableRenderer renderer;

	internal UniversalRenderPipeline(UniversalRenderPipelineAsset asset)
	{
		asset.ValidateConfiguration();
		var shadowSettings = new DirectionalShadowCascadeSettings
		{
			CascadeCount = asset.MainLightShadowCascadeCount,
			ShadowDistance = asset.MainLightShadowDistance,
			SplitFractions = asset.MainLightShadowSplitFractions ?? Array.Empty<float>(),
			AtlasWidth = asset.MainLightShadowAtlasWidth,
			AtlasHeight = asset.MainLightShadowAtlasHeight
		};
		UniversalRendererData data = asset.RendererData ??
			throw new InvalidOperationException("UniversalRenderPipelineAsset requires RendererData.");
		data.RequireValid();
		ScriptableRendererFeature[] configurations = data.Features ?? Array.Empty<ScriptableRendererFeature>();
		var identities = new HashSet<string>(StringComparer.Ordinal);
		var created = new List<RendererFeature>(configurations.Length);
		try
		{
			foreach (ScriptableRendererFeature? configuration in configurations)
			{
				if (configuration == null) throw new InvalidOperationException("Renderer feature list contains an empty reference.");
				configuration.RequireValid();
				if (configuration.AssetReference is not AssetReference reference)
					throw new InvalidOperationException("Renderer feature configuration must be a saved asset.");
				string identity = $"{reference.Guid}:{reference.LocalFileId}";
				if (!identities.Add(identity))
					throw new InvalidOperationException($"Renderer feature '{identity}' is referenced more than once.");
				if (configuration.Enabled)
				{
					RendererFeature instance = configuration.CreateFeatureInstance();
					foreach (RendererFeature previous in created)
						if (ReferenceEquals(previous, instance))
							throw new InvalidOperationException("Renderer feature factories must return independent runtime instances.");
					created.Add(instance);
				}
			}
			features = created.ToArray();
			renderer = new ScriptableRenderer(asset.MainLightShadowsEnabled, shadowSettings, asset.ExposureEV,
				asset.ToneMapping, asset.FxaaEnabled, asset.BloomEnabled, asset.BloomThreshold, asset.BloomIntensity,
				asset.BloomScatter);
		}
		catch (Exception creationError)
		{
			List<Exception>? cleanupErrors = null;
			for (int index = created.Count - 1; index >= 0; --index)
			{
				try { created[index].Dispose(); }
				catch (Exception cleanupError) { (cleanupErrors ??= new()).Add(cleanupError); }
			}
			if (cleanupErrors == null) throw;
			cleanupErrors.Insert(0, creationError);
			throw new AggregateException("Universal renderer feature creation and cleanup failed.", cleanupErrors);
		}
	}

	public override void Render(ScriptableRenderContext context, List<Ursa.Rendering.Camera> cameras)
	{
		foreach (Camera camera in cameras) renderer.RenderCamera(context, camera, features);
	}

	protected override void Dispose(bool disposing)
	{
		if (!disposing) return;
		List<Exception>? errors = null;
		for (int index = features.Length - 1; index >= 0; --index)
		{
			try { features[index].Dispose(); }
			catch (Exception error) { (errors ??= new()).Add(error); }
		}
		try { renderer.Dispose(); }
		catch (Exception error) { (errors ??= new()).Add(error); }
		if (errors != null) throw new AggregateException("Universal renderer cleanup failed.", errors);
	}
}
