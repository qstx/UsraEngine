using System;
using System.Collections.Generic;
using System.Runtime.ExceptionServices;
using Ursa;

namespace Ursa.Rendering;

public enum RenderPassEvent : int
{
	BeforeRendering = 0,
	BeforeOpaques = 100,
	Opaques = 200,
	AfterOpaques = 300,
	BeforeTransparents = 400,
	Transparents = 500,
	AfterTransparents = 600,
	BeforePostProcessing = 650,
	AfterPostProcessing = 700,
	BeforeFinalOutput = 750,
	AfterRendering = 800
}

[Flags]
public enum ScriptableRenderPassInput : uint
{
	None = 0,
	Color = 1,
	Depth = 2,
	Normal = 4
}

public enum ToneMappingMode : int
{
	None = 0,
	Reinhard = 1
}

public abstract class ScriptableRenderPass
{
	private ProfilingSampler? profilingSampler;
	public RenderPassEvent RenderPassEvent { get; set; } = RenderPassEvent.AfterOpaques;
	public ScriptableRenderPassInput Input { get; set; }
	public uint CameraTypeMask { get; set; } = uint.MaxValue;

	public abstract void Execute(ScriptableRenderContext context, Camera camera, CommandBuffer commands);

	internal ProfilingSampler GetProfilingSampler() =>
		profilingSampler ??= new ProfilingSampler($"Feature Pass: {GetType().FullName}");

	internal bool MatchesCameraType(CameraType cameraType) =>
		Enum.IsDefined(cameraType) && (CameraTypeMask & (1u << (int)cameraType)) != 0;

	internal bool HasValidEventAndInputs() =>
		Enum.IsDefined(RenderPassEvent) && AreInputsAvailableAt(RenderPassEvent, Input);

	internal static bool AreInputsAvailableAt(RenderPassEvent passEvent, ScriptableRenderPassInput input)
	{
		const ScriptableRenderPassInput knownInputs =
			ScriptableRenderPassInput.Color | ScriptableRenderPassInput.Depth | ScriptableRenderPassInput.Normal;
		if (!Enum.IsDefined(passEvent) || (input & ~knownInputs) != 0)
			return false;
		if ((input & ScriptableRenderPassInput.Color) != 0 && passEvent < RenderPassEvent.AfterOpaques)
			return false;
		if ((input & (ScriptableRenderPassInput.Depth | ScriptableRenderPassInput.Normal)) != 0 &&
		    passEvent < RenderPassEvent.BeforeOpaques)
			return false;
		return true;
	}
}

internal sealed class CameraRenderPassQueue
{
	private readonly struct Entry
	{
		internal readonly ScriptableRenderPass Pass;
		internal readonly long Sequence;
		internal Entry(ScriptableRenderPass pass, long sequence) { Pass = pass; Sequence = sequence; }
	}

	private readonly List<Entry> entries = new();
	private long nextSequence;

	internal int Count => entries.Count;

	internal void Enqueue(ScriptableRenderPass pass)
	{
		ArgumentNullException.ThrowIfNull(pass);
		entries.Add(new Entry(pass, nextSequence++));
	}

	internal List<ScriptableRenderPass> OrderFor(CameraType cameraType)
	{
		if (!Enum.IsDefined(cameraType)) throw new ArgumentOutOfRangeException(nameof(cameraType));
		var selected = new List<Entry>(entries.Count);
		foreach (Entry entry in entries)
		{
			if (!entry.Pass.HasValidEventAndInputs())
				throw new InvalidOperationException(
					$"Render pass '{entry.Pass.GetType().FullName}' requested inputs '{entry.Pass.Input}' at unsupported event '{entry.Pass.RenderPassEvent}'.");
			if (entry.Pass.MatchesCameraType(cameraType)) selected.Add(entry);
		}
		selected.Sort(static (left, right) =>
		{
			int eventOrder = left.Pass.RenderPassEvent.CompareTo(right.Pass.RenderPassEvent);
			return eventOrder != 0 ? eventOrder : left.Sequence.CompareTo(right.Sequence);
		});
		var result = new List<ScriptableRenderPass>(selected.Count);
		foreach (Entry entry in selected) result.Add(entry.Pass);
		return result;
	}

	internal void Clear()
	{
		entries.Clear();
		nextSequence = 0;
	}
}

public sealed class ScriptableRenderer
{
	private sealed class OutputBacking
	{
		internal readonly RenderTexture texture;
		internal bool initialized;
		internal OutputBacking(RenderTexture texture) => this.texture = texture;
	}

	private const int MaximumOutputBackings = 8;
	private const ulong MaximumOutputBackingBytes = 512ul * 1024 * 1024;

	private readonly CameraRenderPassQueue passes = new();
	private readonly HashSet<ScriptableRenderPass> featurePasses = new();
	private readonly CommandBuffer commands = new("Universal Renderer");
	private readonly DrawingSettings opaqueDrawing = new()
	{
		ShaderTags = new[] { "SRPDefaultUnlit", "UniversalForward", "ForwardBase" },
		Sorting = SortingCriteria.CommonOpaque,
		ActivePipeline = "UrsaUniversal"
	};
	private readonly DrawingSettings transparentDrawing = new()
	{
		ShaderTags = new[] { "SRPDefaultUnlit", "UniversalForward", "ForwardBase" },
		Sorting = SortingCriteria.CommonTransparent,
		ActivePipeline = "UrsaUniversal"
	};
	private readonly FilteringSettings opaqueFiltering = new() { MaximumQueue = 2500 };
	private readonly FilteringSettings transparentFiltering = new() { MinimumQueue = 2501 };
	private readonly ProfilingSampler opaqueSampler = new("Universal Opaques");
	private readonly ProfilingSampler transparentSampler = new("Universal Transparents");
	private readonly ProfilingSampler shadowSampler = new("Main Light Shadows");
	private readonly ProfilingSampler postProcessingSampler = new("Post Processing");
	private readonly ProfilingSampler finalOutputSampler = new("Final Output");
	private readonly ProfilingSampler colorInputSampler = new("Camera Color Input");
	private readonly ProfilingSampler depthInputSampler = new("Camera Depth Input");
	private readonly ProfilingSampler normalInputSampler = new("Camera Normal Input");
	private readonly ProfilingSampler bloomSampler = new("Bloom");
	private readonly bool mainLightShadowsEnabled;
	private readonly DirectionalShadowCascadeSettings shadowSettings;
	private readonly float exposureEV;
	private readonly ToneMappingMode toneMapping;
	private readonly bool fxaaEnabled;
	private readonly bool bloomEnabled;
	private readonly float bloomThreshold;
	private readonly float bloomIntensity;
	private readonly float bloomScatter;
	private TemporaryRenderTargetScope? activeTemporaryScope;
	private readonly RenderOutputBackingCache<OutputBacking> outputBackings;
	private RenderTargetIdentifier activeSceneTarget;
	private OutputBacking? activeOutputBacking;
	private Material? finalOutputMaterial;
	private Material? bloomMaterial;
	private RenderTexture? neutralShadowAtlas;
	private bool collectingFeaturePasses;
	private bool disposed;

	internal ScriptableRenderer(bool mainLightShadowsEnabled, DirectionalShadowCascadeSettings shadowSettings,
		float exposureEV, ToneMappingMode toneMapping, bool fxaaEnabled, bool bloomEnabled = false,
		float bloomThreshold = 1, float bloomIntensity = 0.5f, float bloomScatter = 0.7f)
	{
		this.mainLightShadowsEnabled = mainLightShadowsEnabled;
		this.shadowSettings = shadowSettings;
		this.exposureEV = exposureEV;
		this.toneMapping = toneMapping;
		this.fxaaEnabled = fxaaEnabled;
		if (!float.IsFinite(bloomThreshold) || bloomThreshold < 0 || !float.IsFinite(bloomIntensity) || bloomIntensity < 0 ||
		    !float.IsFinite(bloomScatter) || bloomScatter < 0 || bloomScatter > 1)
			throw new ArgumentOutOfRangeException(nameof(bloomThreshold), "Bloom threshold/intensity must be finite and non-negative; scatter must be finite and in [0, 1].");
		this.bloomEnabled = bloomEnabled;
		this.bloomThreshold = bloomThreshold;
		this.bloomIntensity = bloomIntensity;
		this.bloomScatter = bloomScatter;
		outputBackings = new RenderOutputBackingCache<OutputBacking>(MaximumOutputBackings, MaximumOutputBackingBytes,
			(width, height) => new OutputBacking(RenderTexture.Create(new RenderTextureDescriptor(
				width, height, GraphicsFormat.R16G16B16A16_SFloat, depthBufferBits: 32))),
			backing => backing.texture.Release());
	}

	public void EnqueuePass(ScriptableRenderPass pass)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		ArgumentNullException.ThrowIfNull(pass);
		passes.Enqueue(pass);
		if (collectingFeaturePasses) featurePasses.Add(pass);
	}

	/// <summary>Gets the current camera's linear scene color target while collecting its passes.</summary>
	public RenderTargetIdentifier CameraColorTarget
	{
		get
		{
			ObjectDisposedException.ThrowIf(disposed, this);
			if (activeOutputBacking == null)
				throw new InvalidOperationException("The camera color target is only available while a camera is being rendered.");
			return activeSceneTarget;
		}
	}

	internal void RenderCamera(ScriptableRenderContext context, Camera camera, RendererFeature[] features)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		passes.Clear();
		featurePasses.Clear();
		bool cameraStarted = false;
		Exception? renderError = null;
		try
		{
			context.SetupCameraProperties(camera);
			cameraStarted = true;
			RenderPipelineManager.BeginCamera(context, camera);
			activeOutputBacking = GetOutputBacking(context, camera);
			activeSceneTarget = new RenderTargetIdentifier(activeOutputBacking.texture);
			CullingResults culling = context.Cull(camera);
			VisibleLight[] visibleLights = context.GetVisibleLights(culling);
			activeTemporaryScope = commands.CreateTemporaryTargetScope();
			RendererList opaque = context.CreateRendererList(culling, opaqueDrawing, opaqueFiltering);
			RendererList transparent = context.CreateRendererList(culling, transparentDrawing, transparentFiltering);
			collectingFeaturePasses = true;
			try
			{
				foreach (RendererFeature feature in features) feature.AddRenderPasses(this, camera);
			}
			finally { collectingFeaturePasses = false; }
			EnqueuePass(new ClearCameraPass(camera));
			EnqueuePass(new MainLightShadowCasterPass(this, camera, visibleLights));
			EnqueuePass(new DrawRendererListPass(RenderPassEvent.Opaques, opaque, opaqueSampler));
			EnqueuePass(new DrawRendererListPass(RenderPassEvent.Transparents, transparent, transparentSampler));
			List<ScriptableRenderPass> orderedPasses = passes.OrderFor(camera.Type);
			ScriptableRenderPassInput cameraInputs = CollectInputRequirements(orderedPasses);
			SetCameraTargetAndViewport(commands, camera, activeSceneTarget);
			context.SetCameraColorTarget(activeSceneTarget);
			if (!activeOutputBacking.initialized)
			{
				commands.SetViewport(0, 0, camera.PixelWidth, camera.PixelHeight);
				commands.SetScissor(0, 0, camera.PixelWidth, camera.PixelHeight);
				commands.ClearTarget(true, true, false, default);
				SetCameraTargetAndViewport(commands, camera, activeSceneTarget);
			}
			RenderPassEvent? colorSnapshotEvent = null;
			RenderTargetIdentifier depthInput = default;
			RenderTargetIdentifier normalInput = default;
			bool depthInputGenerated = false;
			bool normalInputGenerated = false;
			bool postProcessingRecorded = false;
			foreach (ScriptableRenderPass pass in orderedPasses)
			{
				if (!postProcessingRecorded && pass.RenderPassEvent > RenderPassEvent.BeforePostProcessing)
				{
					activeSceneTarget = RecordPostProcessing(camera);
					context.SetCameraColorTarget(activeSceneTarget);
					colorSnapshotEvent = null;
					postProcessingRecorded = true;
				}
				if (!pass.HasValidEventAndInputs())
					throw new InvalidOperationException(
						$"Camera '{camera.PersistentId}' render pass '{pass.GetType().FullName}' requested inputs '{pass.Input}' at unsupported event '{pass.RenderPassEvent}'.");
				SetCameraTargetAndViewport(commands, camera, activeSceneTarget);
				context.SetCameraColorTarget(activeSceneTarget);
				ScriptableRenderPassInput requested = pass.Input;
				if ((requested & ScriptableRenderPassInput.Color) != 0 &&
				    NeedsColorSnapshot(colorSnapshotEvent, pass.RenderPassEvent))
				{
					if (activeTemporaryScope == null)
						throw new InvalidOperationException("Color pass input requires an active camera temporary target scope.");
					RenderTargetIdentifier snapshot = activeTemporaryScope.Allocate(new RenderTextureDescriptor(
						camera.PixelWidth, camera.PixelHeight, GraphicsFormat.R16G16B16A16_SFloat));
					using (new ProfilingScope(commands, colorInputSampler))
					{
						commands.CopyTexture(activeSceneTarget, snapshot);
						commands.SetGlobalTexture("_CameraColorTexture", snapshot);
					}
					colorSnapshotEvent = pass.RenderPassEvent;
				}
				if ((cameraInputs & ScriptableRenderPassInput.Depth) != 0 &&
				    (requested & ScriptableRenderPassInput.Depth) != 0 && !depthInputGenerated)
				{
					depthInput = GenerateDepthInput(context, camera, culling, pass);
					depthInputGenerated = true;
				}
				if ((cameraInputs & ScriptableRenderPassInput.Normal) != 0 &&
				    (requested & ScriptableRenderPassInput.Normal) != 0 && !normalInputGenerated)
				{
					normalInput = GenerateNormalInput(context, camera, culling, pass);
					normalInputGenerated = true;
				}
				if ((requested & ScriptableRenderPassInput.Depth) != 0)
					commands.SetGlobalTexture("_CameraDepthTexture", depthInput);
				if ((requested & ScriptableRenderPassInput.Normal) != 0)
					commands.SetGlobalTexture("_CameraNormalsTexture", normalInput);
				if (featurePasses.Contains(pass))
				{
					using (new ProfilingScope(commands, pass.GetProfilingSampler()))
						pass.Execute(context, camera, commands);
				}
				else
				{
					pass.Execute(context, camera, commands);
				}
			}
			if (!postProcessingRecorded)
			{
				activeSceneTarget = RecordPostProcessing(camera);
				context.SetCameraColorTarget(activeSceneTarget);
			}
			RecordFinalOutput(camera, true);
			activeTemporaryScope.Dispose();
			activeTemporaryScope = null;
			context.ExecuteCommandBuffer(commands);
			context.Submit();
			activeOutputBacking.initialized = true;
		}
		catch (Exception error)
		{
			renderError = error;
		}
		List<Exception>? cleanupErrors = null;
		void Cleanup(Action action)
		{
			try { action(); }
			catch (Exception error) { (cleanupErrors ??= new()).Add(error); }
		}
		Cleanup(() => activeTemporaryScope?.Dispose());
		activeTemporaryScope = null;
		Cleanup(commands.Clear);
		passes.Clear();
		featurePasses.Clear();
		collectingFeaturePasses = false;
		activeOutputBacking = null;
		context.ClearCameraColorTarget();
		if (cameraStarted) Cleanup(() => RenderPipelineManager.EndCamera(context, camera));
		if (renderError != null)
		{
			if (cleanupErrors != null)
			{
				cleanupErrors.Insert(0, renderError);
				throw new AggregateException("Universal camera rendering and cleanup failed.", cleanupErrors);
			}
			ExceptionDispatchInfo.Capture(renderError).Throw();
		}
		if (cleanupErrors != null)
			throw new AggregateException("Universal camera cleanup failed.", cleanupErrors);
	}

	public void Dispose()
	{
		if (disposed) return;
		disposed = true;
		passes.Clear();
		List<Exception>? errors = null;
		try { outputBackings.Dispose(); }
		catch (AggregateException error) { (errors ??= new()).AddRange(error.InnerExceptions); }
		try { finalOutputMaterial?.Destroy(); }
		catch (Exception error) { (errors ??= new()).Add(error); }
		try { bloomMaterial?.Destroy(); }
		catch (Exception error) { (errors ??= new()).Add(error); }
		try { neutralShadowAtlas?.Release(); }
		catch (Exception error) { (errors ??= new()).Add(error); }
		try { commands.Dispose(); }
		catch (Exception error) { (errors ??= new()).Add(error); }
		if (errors != null) throw new AggregateException("Scriptable renderer cleanup failed.", errors);
	}

	internal void RenderMainLightShadows(ScriptableRenderContext context, Camera camera, VisibleLight[] visibleLights)
	{
		int mainLightIndex = SelectMainDirectionalLight(visibleLights);
		commands.SetGlobalInt("_UrsaMainLightIndex", mainLightIndex);
		if (!ShouldRenderMainLightShadows(mainLightShadowsEnabled, visibleLights, mainLightIndex))
		{
			BindNeutralShadowAtlas(camera);
			return;
		}

		VisibleLight mainLight = visibleLights[mainLightIndex];
		if (mainLight.Type != VisibleLightType.Directional)
			throw new InvalidOperationException("Main light selection returned a non-directional light.");
		DirectionalShadowCascade[] cascades = context.ComputeDirectionalShadowCascades(camera, mainLight.Direction, shadowSettings);
		if (cascades.Length != (int)shadowSettings.CascadeCount)
			throw new InvalidOperationException("Directional shadow cascade computation returned an unexpected count.");
		if (activeTemporaryScope == null)
			throw new InvalidOperationException("Main light shadows require an active camera temporary target scope.");

		using (new ProfilingScope(commands, shadowSampler))
		{
			RenderTargetIdentifier atlas = activeTemporaryScope.Allocate(new RenderTextureDescriptor(
				shadowSettings.AtlasWidth, shadowSettings.AtlasHeight, GraphicsFormat.D32_SFloat));
			commands.SetRenderTargets(ReadOnlySpan<RenderTargetIdentifier>.Empty, atlas);
			commands.SetViewport(0, 0, shadowSettings.AtlasWidth, shadowSettings.AtlasHeight);
			commands.SetScissor(0, 0, shadowSettings.AtlasWidth, shadowSettings.AtlasHeight);
			commands.ClearTarget(false, true, false, default);
			commands.SetGlobalTexture("_UrsaDirectionalShadowAtlas", atlas);
			SetShadowGlobals(mainLight, cascades);

			var drawing = new DrawingSettings
			{
				ShaderTags = new[] { "ShadowCaster" },
				Sorting = SortingCriteria.CommonOpaque,
				ActivePipeline = "UrsaUniversal"
			};
			var filtering = new FilteringSettings { MaximumQueue = 2500, LayerMask = camera.CullingMask };
			foreach (DirectionalShadowCascade cascade in cascades)
			{
				Matrix4x4 lightViewProjection = cascade.LightProjection * cascade.LightView;
				CullingResults shadowCasters = context.CullShadowCasters(camera, lightViewProjection);
				RendererList list = context.CreateRendererList(shadowCasters, drawing, filtering);
				commands.SetViewport(cascade.AtlasX, cascade.AtlasY, cascade.Resolution, cascade.Resolution);
				commands.SetScissor((int)cascade.AtlasX, (int)cascade.AtlasY, cascade.Resolution, cascade.Resolution);
				commands.SetViewProjectionMatrix(lightViewProjection);
				commands.DrawRendererList(list, context);
			}
		}
		commands.RestoreViewProjectionMatrix(camera);
		SetCameraTargetAndViewport(commands, camera, activeSceneTarget);
	}

	internal static int SelectMainDirectionalLight(VisibleLight[] visibleLights)
	{
		int selected = -1;
		for (int index = 0; index < visibleLights.Length; ++index)
		{
			VisibleLight candidate = visibleLights[index];
			if (candidate.Type != VisibleLightType.Directional) continue;
			if (selected < 0 || candidate.Intensity > visibleLights[selected].Intensity ||
			    (candidate.Intensity == visibleLights[selected].Intensity &&
			     string.CompareOrdinal(candidate.Identity, visibleLights[selected].Identity) < 0))
				selected = index;
		}
		return selected;
	}

	internal static bool ShouldRenderMainLightShadows(bool shadowsEnabled, VisibleLight[] visibleLights, int mainLightIndex)
	{
		ArgumentNullException.ThrowIfNull(visibleLights);
		return shadowsEnabled && (uint)mainLightIndex < (uint)visibleLights.Length &&
		       visibleLights[mainLightIndex].Type == VisibleLightType.Directional &&
		       visibleLights[mainLightIndex].CastsShadows;
	}

	private void SetShadowGlobals(VisibleLight mainLight, DirectionalShadowCascade[] cascades)
	{
		var matrices = new Matrix4x4[4];
		for (int index = 0; index < matrices.Length; ++index) matrices[index] = Matrix4x4.Identity;
		var splits = new float[4];
		for (int index = 0; index < cascades.Length; ++index)
		{
			matrices[index] = cascades[index].WorldToShadow;
			splits[index] = cascades[index].FarDepth;
		}
		commands.SetGlobalMatrixArray("_UrsaDirectionalShadowMatrices", matrices);
		commands.SetGlobalVector("_UrsaDirectionalShadowCascadeSplits", new Vector4(splits[0], splits[1], splits[2], splits[3]));
		commands.SetGlobalInt("_UrsaDirectionalShadowCascadeCount", cascades.Length);
		commands.SetGlobalInt("_UrsaDirectionalShadowSoft", mainLight.SoftShadows ? 1 : 0);
		commands.SetGlobalFloat("_UrsaDirectionalShadowStrength", mainLight.ShadowStrength);
		commands.SetGlobalFloat("_UrsaDirectionalShadowBias", mainLight.ShadowBias);
		commands.SetGlobalFloat("_UrsaDirectionalShadowNormalBias", mainLight.ShadowNormalBias);
		commands.SetGlobalFloat("_UrsaDirectionalShadowCascadeBlend", 0.1f);
		commands.SetGlobalVector("_UrsaDirectionalShadowTexelSize", new Vector4(1.0f / shadowSettings.AtlasWidth,
			1.0f / shadowSettings.AtlasHeight, shadowSettings.AtlasWidth, shadowSettings.AtlasHeight));
		commands.SetGlobalVector("_UrsaMainLightDirection", new Vector4(mainLight.Direction.X, mainLight.Direction.Y,
			mainLight.Direction.Z, 0));
	}

	private void BindNeutralShadowAtlas(Camera camera)
	{
		neutralShadowAtlas ??= RenderTexture.Create(new RenderTextureDescriptor(1, 1, GraphicsFormat.D32_SFloat));
		commands.SetRenderTarget(neutralShadowAtlas);
		commands.SetViewport(0, 0, 1, 1);
		commands.SetScissor(0, 0, 1, 1);
		commands.ClearTarget(false, true, false, default);
		commands.SetGlobalTexture("_UrsaDirectionalShadowAtlas", neutralShadowAtlas);
		commands.SetGlobalInt("_UrsaDirectionalShadowCascadeCount", 0);
		commands.SetGlobalVector("_UrsaDirectionalShadowCascadeSplits", default);
		commands.SetGlobalVector("_UrsaDirectionalShadowTexelSize", new Vector4(1, 1, 1, 1));
		commands.SetGlobalFloat("_UrsaDirectionalShadowStrength", 0);
		commands.SetGlobalFloat("_UrsaDirectionalShadowBias", 0);
		commands.SetGlobalFloat("_UrsaDirectionalShadowNormalBias", 0);
		commands.SetGlobalFloat("_UrsaDirectionalShadowCascadeBlend", 0.1f);
		commands.SetGlobalInt("_UrsaDirectionalShadowSoft", 0);
		commands.SetGlobalVector("_UrsaMainLightDirection", default);
		commands.RestoreViewProjectionMatrix(camera);
		SetCameraTargetAndViewport(commands, camera, activeSceneTarget);
	}

	private OutputBacking GetOutputBacking(ScriptableRenderContext context, Camera camera)
	{
		return outputBackings.Get(context.SessionId, camera.OutputTargetIdentity, camera.OutputTargetGeneration,
			camera.PixelWidth, camera.PixelHeight).Resource;
	}

	private RenderTargetIdentifier RecordPostProcessing(Camera camera)
	{
		using (new ProfilingScope(commands, postProcessingSampler))
			return RecordPostProcessingCore(camera);
	}

	private RenderTargetIdentifier RecordPostProcessingCore(Camera camera)
	{
		finalOutputMaterial ??= Material.Create(new AssetReference("10000000-0000-4000-8000-000000000004"));
		finalOutputMaterial.Sampler = new MaterialSampler { Linear = true, Repeat = false };
		if (ShouldAllocateBloom(bloomEnabled, bloomIntensity)) RecordBloom(camera);
		if (activeTemporaryScope == null)
			throw new InvalidOperationException("Post processing requires an active camera temporary target scope.");
		RenderTargetIdentifier postProcessed = activeTemporaryScope.Allocate(new RenderTextureDescriptor(
			camera.PixelWidth, camera.PixelHeight, GraphicsFormat.R16G16B16A16_SFloat));
		Viewport viewport = camera.Viewport;
		SetCameraTargetAndViewport(commands, camera, postProcessed);
		commands.SetGlobalFloat("_UrsaExposureEV", exposureEV);
		commands.SetGlobalInt("_UrsaToneMappingMode", (int)toneMapping);
		commands.SetGlobalInt("_UrsaFxaaEnabled", fxaaEnabled ? 1 : 0);
		commands.SetGlobalInt("_UrsaFinalPostOutputMode", 1);
		commands.SetGlobalFloat("_UrsaBloomIntensity", bloomEnabled && bloomIntensity > 0 ? bloomIntensity : 0);
		commands.SetGlobalVector("_UrsaFinalSourceRect", new Vector4(viewport.X, viewport.Y, viewport.Width, viewport.Height));
		commands.SetGlobalVector("_UrsaFinalSourceSize", new Vector4(1.0f / camera.PixelWidth,
			1.0f / camera.PixelHeight, camera.PixelWidth, camera.PixelHeight));
		commands.SetGlobalTexture("_UrsaBloomTexture",
			bloomEnabled && bloomIntensity > 0 && bloomMaterial != null ? bloomTexture : activeSceneTarget);
		commands.Blit(activeSceneTarget, postProcessed, finalOutputMaterial, 0);
		return postProcessed;
	}

	private void RecordFinalOutput(Camera camera, bool inputIsPostProcessed)
	{
		finalOutputMaterial ??= Material.Create(new AssetReference("10000000-0000-4000-8000-000000000004"));
		finalOutputMaterial.Sampler = new MaterialSampler { Linear = true, Repeat = false };
		Viewport viewport = camera.Viewport;
		SetCameraTargetAndViewport(commands, camera, RenderTargetIdentifier.CameraTarget);
		commands.SetGlobalInt("_UrsaFinalPostOutputMode", inputIsPostProcessed ? 2 : 0);
		commands.SetGlobalInt("_UrsaFinalOutputIsSrgb", camera.OutputIsSrgb ? 1 : 0);
		commands.SetGlobalFloat("_UrsaExposureEV", inputIsPostProcessed ? 0 : exposureEV);
		commands.SetGlobalInt("_UrsaToneMappingMode", inputIsPostProcessed ? (int)ToneMappingMode.None : (int)toneMapping);
		commands.SetGlobalInt("_UrsaFxaaEnabled", inputIsPostProcessed ? 0 : (fxaaEnabled ? 1 : 0));
		commands.SetGlobalFloat("_UrsaBloomIntensity", 0);
		commands.SetGlobalVector("_UrsaFinalSourceRect", new Vector4(viewport.X, viewport.Y, viewport.Width, viewport.Height));
		commands.SetGlobalVector("_UrsaFinalSourceSize", new Vector4(1.0f / camera.PixelWidth,
			1.0f / camera.PixelHeight, camera.PixelWidth, camera.PixelHeight));
		commands.SetGlobalTexture("_UrsaBloomTexture", activeSceneTarget);
		using (new ProfilingScope(commands, finalOutputSampler))
			commands.Blit(activeSceneTarget, RenderTargetIdentifier.CameraTarget, finalOutputMaterial, 0);
	}

	private RenderTargetIdentifier bloomTexture;

	internal static bool ShouldAllocateBloom(bool enabled, float intensity) => enabled && intensity > 0;

	internal static ScriptableRenderPassInput CollectInputRequirements(IReadOnlyList<ScriptableRenderPass> passes)
	{
		ArgumentNullException.ThrowIfNull(passes);
		ScriptableRenderPassInput requirements = ScriptableRenderPassInput.None;
		foreach (ScriptableRenderPass pass in passes)
		{
			ArgumentNullException.ThrowIfNull(pass);
			requirements |= pass.Input;
		}
		return requirements;
	}

	internal static bool NeedsColorSnapshot(RenderPassEvent? capturedEvent, RenderPassEvent requestedEvent) =>
		capturedEvent != requestedEvent;

	private void RecordBloom(Camera camera)
	{
		using (new ProfilingScope(commands, bloomSampler))
			RecordBloomCore(camera);
	}

	private void RecordBloomCore(Camera camera)
	{
		if (activeTemporaryScope == null) throw new InvalidOperationException("Bloom requires an active camera temporary target scope.");
		bloomMaterial ??= Material.Create(new AssetReference("10000000-0000-4000-8000-000000000005"));
		bloomMaterial.Sampler = new MaterialSampler { Linear = true, Repeat = false };
		var pyramid = new List<RenderTargetIdentifier>(6);
		var sizes = new List<(uint Width, uint Height)>(6);
		Viewport viewport = camera.Viewport;
		int left = (int)MathF.Floor(viewport.X * camera.PixelWidth);
		int top = (int)MathF.Floor(viewport.Y * camera.PixelHeight);
		uint right = (uint)MathF.Ceiling((viewport.X + viewport.Width) * camera.PixelWidth);
		uint bottom = (uint)MathF.Ceiling((viewport.Y + viewport.Height) * camera.PixelHeight);
		uint viewportWidth = right - (uint)left;
		uint viewportHeight = bottom - (uint)top;
		uint width = Math.Max(1, viewportWidth / 2);
		uint height = Math.Max(1, viewportHeight / 2);
		commands.SetGlobalFloat("_UrsaBloomThreshold", bloomThreshold);
		commands.SetGlobalFloat("_UrsaBloomExposureEV", exposureEV);
		commands.SetGlobalVector("_UrsaBloomSourceRect",
			new Vector4(viewport.X, viewport.Y, viewport.Width, viewport.Height));
		for (int level = 0; level < 6; ++level)
		{
			pyramid.Add(activeTemporaryScope.Allocate(
				new RenderTextureDescriptor(width, height, GraphicsFormat.R16G16B16A16_SFloat)));
			sizes.Add((width, height));
			if (level == 0)
			{
				commands.Blit(activeSceneTarget, pyramid[level], bloomMaterial, 0);
			}
			else
			{
				commands.SetGlobalVector("_UrsaBloomSourceRect", new Vector4(0, 0, 1, 1));
				(uint sourceWidth, uint sourceHeight) = sizes[level - 1];
				commands.SetGlobalVector("_UrsaBloomTexelSize", new Vector4(1.0f / sourceWidth,
					1.0f / sourceHeight, sourceWidth, sourceHeight));
				commands.Blit(pyramid[level - 1], pyramid[level], bloomMaterial, 1);
			}
			if (width == 1 && height == 1) break;
			width = Math.Max(1, width / 2);
			height = Math.Max(1, height / 2);
		}
		RenderTargetIdentifier current = pyramid[^1];
		for (int level = pyramid.Count - 2; level >= 0; --level)
		{
			(uint widthAtLevel, uint heightAtLevel) = sizes[level];
			RenderTargetIdentifier upsampled = activeTemporaryScope.Allocate(
				new RenderTextureDescriptor(widthAtLevel, heightAtLevel, GraphicsFormat.R16G16B16A16_SFloat));
			RenderTargetIdentifier lowMip = current;
			commands.SetGlobalTexture("_UrsaBloomLowMip", current);
			commands.SetGlobalFloat("_UrsaBloomScatter", bloomScatter);
			commands.Blit(pyramid[level], upsampled, bloomMaterial, 2);
			activeTemporaryScope.Release(lowMip);
			activeTemporaryScope.Release(pyramid[level]);
			current = upsampled;
		}
		bloomTexture = current;
	}

	private RenderTargetIdentifier GenerateDepthInput(ScriptableRenderContext context, Camera camera,
		CullingResults culling, ScriptableRenderPass consumer)
	{
		using (new ProfilingScope(commands, depthInputSampler))
			return GenerateDepthInputCore(context, camera, culling, consumer);
	}

	private RenderTargetIdentifier GenerateDepthInputCore(ScriptableRenderContext context, Camera camera,
		CullingResults culling, ScriptableRenderPass consumer)
	{
		if (activeTemporaryScope == null) throw new InvalidOperationException("Depth input requires an active camera temporary target scope.");
		RenderTargetIdentifier depth = activeTemporaryScope.Allocate(new RenderTextureDescriptor(
			camera.PixelWidth, camera.PixelHeight, GraphicsFormat.D32_SFloat));
		commands.RestoreViewProjectionMatrix(camera);
		commands.SetRenderTarget(depth);
		commands.SetViewport(0, 0, camera.PixelWidth, camera.PixelHeight);
		commands.SetScissor(0, 0, camera.PixelWidth, camera.PixelHeight);
		commands.ClearTarget(false, true, false, default);
		SetCameraTargetAndViewport(commands, camera, depth);
		var drawing = new DrawingSettings
		{
			ShaderTags = new[] { "DepthOnly" },
			Sorting = SortingCriteria.CommonOpaque,
			CameraPosition = camera.Position,
			ActivePipeline = "UrsaUniversal"
		};
		var filtering = new FilteringSettings { MaximumQueue = 2500, LayerMask = camera.CullingMask };
		RendererList list;
		try { list = context.CreateRendererList(culling, drawing, filtering); }
		catch (InvalidOperationException error)
		{
			throw new InvalidOperationException(
				$"Camera '{camera.PersistentId}' pass '{consumer.GetType().FullName}' requires DepthOnly input: {error.Message}", error);
		}
		commands.DrawRendererList(list, context);
		commands.RestoreViewProjectionMatrix(camera);
		SetCameraTargetAndViewport(commands, camera, activeSceneTarget);
		return depth;
	}

	private RenderTargetIdentifier GenerateNormalInput(ScriptableRenderContext context, Camera camera,
		CullingResults culling, ScriptableRenderPass consumer)
	{
		using (new ProfilingScope(commands, normalInputSampler))
			return GenerateNormalInputCore(context, camera, culling, consumer);
	}

	private RenderTargetIdentifier GenerateNormalInputCore(ScriptableRenderContext context, Camera camera,
		CullingResults culling, ScriptableRenderPass consumer)
	{
		if (activeTemporaryScope == null) throw new InvalidOperationException("Normal input requires an active camera temporary target scope.");
		RenderTargetIdentifier normals = activeTemporaryScope.Allocate(new RenderTextureDescriptor(camera.PixelWidth,
			camera.PixelHeight, GraphicsFormat.R16G16B16A16_SFloat, depthBufferBits: 32));
		commands.RestoreViewProjectionMatrix(camera);
		commands.SetRenderTargets(new[] { normals }, normals);
		commands.SetViewport(0, 0, camera.PixelWidth, camera.PixelHeight);
		commands.SetScissor(0, 0, camera.PixelWidth, camera.PixelHeight);
		// A cleared pixel has no surface normal; keeping every channel zero lets
		// consumers distinguish background from an actual +Z-facing surface.
		commands.ClearTarget(true, true, false, default);
		SetCameraViewportAndScissor(commands, camera);
		var drawing = new DrawingSettings
		{
			ShaderTags = new[] { "DepthNormals" },
			Sorting = SortingCriteria.CommonOpaque,
			CameraPosition = camera.Position,
			ActivePipeline = "UrsaUniversal"
		};
		var filtering = new FilteringSettings { MaximumQueue = 2500, LayerMask = camera.CullingMask };
		RendererList list;
		try { list = context.CreateRendererList(culling, drawing, filtering); }
		catch (InvalidOperationException error)
		{
			throw new InvalidOperationException(
				$"Camera '{camera.PersistentId}' pass '{consumer.GetType().FullName}' requires DepthNormals input: {error.Message}", error);
		}
		commands.DrawRendererList(list, context);
		commands.RestoreViewProjectionMatrix(camera);
		SetCameraTargetAndViewport(commands, camera, activeSceneTarget);
		return normals;
	}

	private static void SetCameraTargetAndViewport(CommandBuffer commands, Camera camera, RenderTargetIdentifier target)
	{
		commands.SetRenderTarget(target);
		SetCameraViewportAndScissor(commands, camera);
	}

	private static void SetCameraViewportAndScissor(CommandBuffer commands, Camera camera)
	{
		Viewport viewport = camera.Viewport;
		commands.SetViewport(viewport.X * camera.PixelWidth, viewport.Y * camera.PixelHeight,
			viewport.Width * camera.PixelWidth, viewport.Height * camera.PixelHeight);
		int left = (int)MathF.Floor(viewport.X * camera.PixelWidth);
		int top = (int)MathF.Floor(viewport.Y * camera.PixelHeight);
		uint right = (uint)MathF.Ceiling((viewport.X + viewport.Width) * camera.PixelWidth);
		uint bottom = (uint)MathF.Ceiling((viewport.Y + viewport.Height) * camera.PixelHeight);
		commands.SetScissor(left, top, right - (uint)left, bottom - (uint)top);
	}

	private sealed class DrawRendererListPass : ScriptableRenderPass
	{
		private readonly RendererList list;
		private readonly ProfilingSampler sampler;
		internal DrawRendererListPass(RenderPassEvent renderPassEvent, RendererList list, ProfilingSampler sampler)
		{
			RenderPassEvent = renderPassEvent;
			this.list = list;
			this.sampler = sampler;
		}
		public override void Execute(ScriptableRenderContext context, Camera camera, CommandBuffer commands)
		{
			using (new ProfilingScope(commands, sampler)) commands.DrawRendererList(list, context);
		}
	}

	private sealed class ClearCameraPass : ScriptableRenderPass
	{
		private readonly Camera camera;
		internal ClearCameraPass(Camera camera) { this.camera = camera; RenderPassEvent = RenderPassEvent.BeforeRendering; }
		public override void Execute(ScriptableRenderContext context, Camera camera, CommandBuffer commands)
		{
			if (this.camera.ClearFlags == CameraClearFlags.Nothing) return;
			Color background = this.camera.Background;
			commands.ClearTarget(this.camera.ClearFlags == CameraClearFlags.ColorAndDepth, true, false,
				new Vector4(background.R, background.G, background.B, background.A));
		}
	}

	private sealed class MainLightShadowCasterPass : ScriptableRenderPass
	{
		private readonly ScriptableRenderer renderer;
		private readonly Camera camera;
		private readonly VisibleLight[] visibleLights;
		internal MainLightShadowCasterPass(ScriptableRenderer renderer, Camera camera, VisibleLight[] visibleLights)
		{
			this.renderer = renderer;
			this.camera = camera;
			this.visibleLights = visibleLights;
			RenderPassEvent = RenderPassEvent.BeforeOpaques;
		}
		public override void Execute(ScriptableRenderContext context, Camera camera, CommandBuffer commands) =>
			renderer.RenderMainLightShadows(context, this.camera, visibleLights);
	}
}
