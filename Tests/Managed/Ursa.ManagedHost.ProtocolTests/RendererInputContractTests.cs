using System;
using System.Collections.Generic;
using Ursa.Rendering;
using Ursa;

internal static class RendererInputContractTests
{
	private sealed class QueueProbePass : ScriptableRenderPass
	{
		internal readonly string Name;
		internal QueueProbePass(string name) => Name = name;
		public override void Execute(ScriptableRenderContext context, Camera camera, CommandBuffer commands) =>
			throw new NotSupportedException("Queue probes are inspected without executing renderer work.");
	}

	internal static void Run()
	{
		if (!ScriptableRenderer.ShouldAllocateBloom(true, 0.5f) || ScriptableRenderer.ShouldAllocateBloom(true, 0) ||
		    ScriptableRenderer.ShouldAllocateBloom(false, 0.5f))
			throw new Exception("Bloom enable/intensity policy would allocate targets for a disabled or zero-strength effect.");
		if (ScriptableRenderer.CollectInputRequirements(Array.Empty<ScriptableRenderPass>()) !=
		    ScriptableRenderPassInput.None)
			throw new Exception("A camera without input consumers requested depth or normals prepasses.");
		var normalConsumer = new QueueProbePass("normal consumer") { Input = ScriptableRenderPassInput.Normal };
		var depthConsumer = new QueueProbePass("depth consumer") { Input = ScriptableRenderPassInput.Depth };
		if (ScriptableRenderer.CollectInputRequirements(new ScriptableRenderPass[] { normalConsumer, depthConsumer }) !=
		    (ScriptableRenderPassInput.Normal | ScriptableRenderPassInput.Depth))
			throw new Exception("Camera input requirements were not shared across all valid consumers.");
		if (!ScriptableRenderer.NeedsColorSnapshot(null, RenderPassEvent.AfterOpaques) ||
		    ScriptableRenderer.NeedsColorSnapshot(RenderPassEvent.AfterOpaques, RenderPassEvent.AfterOpaques) ||
		    !ScriptableRenderer.NeedsColorSnapshot(RenderPassEvent.AfterOpaques, RenderPassEvent.AfterTransparents))
			throw new Exception("Color snapshots were not partitioned by injected pass event.");
		TestMainLightSelection();
		TestDirectionalShadowSettingsValidation();
		Expect(false, RenderPassEvent.BeforeRendering, ScriptableRenderPassInput.Depth,
			"Depth must not be requested before opaque rendering begins.");
		Expect(false, RenderPassEvent.BeforeRendering, ScriptableRenderPassInput.Normal,
			"Normals must not be requested before opaque rendering begins.");
		Expect(false, RenderPassEvent.BeforeOpaques, ScriptableRenderPassInput.Color,
			"Color must not be requested before an opaque color snapshot exists.");
		Expect(false, RenderPassEvent.Opaques, ScriptableRenderPassInput.Color,
			"Color must not be requested during the opaque stage.");
		Expect(true, RenderPassEvent.AfterOpaques, ScriptableRenderPassInput.Color,
			"Color must be available after opaques.");
		Expect(true, RenderPassEvent.AfterTransparents,
			ScriptableRenderPassInput.Color | ScriptableRenderPassInput.Depth | ScriptableRenderPassInput.Normal,
			"All camera inputs must be available after transparents.");
		if (!(RenderPassEvent.AfterTransparents < RenderPassEvent.BeforePostProcessing &&
		      RenderPassEvent.BeforePostProcessing < RenderPassEvent.AfterPostProcessing &&
		      RenderPassEvent.AfterPostProcessing < RenderPassEvent.BeforeFinalOutput &&
		      RenderPassEvent.BeforeFinalOutput < RenderPassEvent.AfterRendering))
			throw new Exception("Scriptable pass events do not preserve the postprocessing color-domain sequence.");
		TestPassQueue();
		Expect(true, RenderPassEvent.BeforePostProcessing, ScriptableRenderPassInput.Color,
			"Color input must be available before postprocessing in HDR linear space.");
		Expect(true, RenderPassEvent.AfterPostProcessing, ScriptableRenderPassInput.Color,
			"Color input must be available after postprocessing in LDR linear space.");
		Expect(true, RenderPassEvent.BeforeOpaques,
			ScriptableRenderPassInput.Depth | ScriptableRenderPassInput.Normal,
			"Depth and normals can be prepared before opaque color rendering.");
		Expect(false, (RenderPassEvent)int.MaxValue, ScriptableRenderPassInput.None,
			"Unknown pass events must be rejected.");
		Expect(false, RenderPassEvent.AfterOpaques, (ScriptableRenderPassInput)8,
			"Unknown input flags must be rejected.");

		var renderer = new ScriptableRenderer(false, new DirectionalShadowCascadeSettings(), 0,
			ToneMappingMode.Reinhard, false);
		bool unavailableOutsideCamera = false;
		try { _ = renderer.CameraColorTarget; }
		catch (InvalidOperationException) { unavailableOutsideCamera = true; }
		finally { renderer.Dispose(); }
		if (!unavailableOutsideCamera)
			throw new Exception("The camera color target was exposed outside camera rendering.");
	}

	private static void TestPassQueue()
	{
		var queue = new CameraRenderPassQueue();
		var transparent = new QueueProbePass("transparent builtin") { RenderPassEvent = RenderPassEvent.Transparents };
		var opaques = new QueueProbePass("opaque builtin") { RenderPassEvent = RenderPassEvent.Opaques };
		var firstAfterOpaques = new QueueProbePass("after-opaques-1") { RenderPassEvent = RenderPassEvent.AfterOpaques };
		var secondAfterOpaques = new QueueProbePass("after-opaques-2") { RenderPassEvent = RenderPassEvent.AfterOpaques };
		firstAfterOpaques.CameraTypeMask = 1u << (int)CameraType.Game;
		secondAfterOpaques.CameraTypeMask = 1u << (int)CameraType.Game;
		var previewPass = new QueueProbePass("preview only") { RenderPassEvent = RenderPassEvent.AfterOpaques };
		previewPass.CameraTypeMask = 1u << (int)CameraType.Preview;
		queue.Enqueue(transparent);
		queue.Enqueue(firstAfterOpaques);
		queue.Enqueue(opaques);
		queue.Enqueue(secondAfterOpaques);
		queue.Enqueue(previewPass);
		IReadOnlyList<ScriptableRenderPass> game = queue.OrderFor(CameraType.Game);
		if (game.Count != 4 || !ReferenceEquals(game[0], opaques) || !ReferenceEquals(game[1], firstAfterOpaques) ||
		    !ReferenceEquals(game[2], secondAfterOpaques) || !ReferenceEquals(game[3], transparent))
			throw new Exception("Pass queue did not sort events stably or filter passes by camera type.");
		IReadOnlyList<ScriptableRenderPass> preview = queue.OrderFor(CameraType.Preview);
		if (preview.Count != 3 || !ReferenceEquals(preview[0], opaques) || !ReferenceEquals(preview[1], previewPass) ||
		    !ReferenceEquals(preview[2], transparent))
			throw new Exception("A camera-type-filtered pass was missing from its selected camera.");

		queue.Clear();
		if (queue.Count != 0 || queue.OrderFor(CameraType.Game).Count != 0)
			throw new Exception("Render passes leaked into the next camera after queue reset.");
		var nextCameraPass = new QueueProbePass("next-camera") { RenderPassEvent = RenderPassEvent.Opaques };
		queue.Enqueue(nextCameraPass);
		IReadOnlyList<ScriptableRenderPass> nextCamera = queue.OrderFor(CameraType.Game);
		if (nextCamera.Count != 1 || !ReferenceEquals(nextCamera[0], nextCameraPass))
			throw new Exception("A subsequent camera did not receive its newly enqueued passes.");

		queue.Clear();
		queue.Enqueue(new QueueProbePass("invalid-input")
		{
			RenderPassEvent = RenderPassEvent.Opaques,
			Input = ScriptableRenderPassInput.Color
		});
		bool invalidInputRejected = false;
		try { queue.OrderFor(CameraType.Preview); }
		catch (InvalidOperationException) { invalidInputRejected = true; }
		if (!invalidInputRejected)
			throw new Exception("Pass queue accepted an input request before its event made the input available.");
	}

	private static void TestDirectionalShadowSettingsValidation()
	{
		new DirectionalShadowCascadeSettings().Validate();
		if (new UniversalRenderPipelineAsset().MainLightShadowAtlasHeight != 1024)
			throw new Exception("The default two-cascade shadow atlas does not use square tiles.");
		new DirectionalShadowCascadeSettings
		{
			CascadeCount = 4,
			SplitFractions = new[] { 0.1f, 0.3f, 0.6f },
			AtlasWidth = 2048,
			AtlasHeight = 2048
		}.Validate();

		bool invalidDefaultLayoutRejected = false;
		try
		{
			new DirectionalShadowCascadeSettings
			{
				CascadeCount = 2,
				SplitFractions = new[] { 0.25f },
				AtlasWidth = 2048,
				AtlasHeight = 2048
			}.Validate();
		}
		catch (ArgumentException) { invalidDefaultLayoutRejected = true; }
		if (!invalidDefaultLayoutRejected)
			throw new Exception("A shadow atlas with non-square cascade tiles was accepted.");

		bool invalidSplitRejected = false;
		try
		{
			new DirectionalShadowCascadeSettings
			{
				CascadeCount = 4,
				SplitFractions = new[] { 0.4f, 0.4f, 0.8f },
				AtlasWidth = 2048,
				AtlasHeight = 2048
			}.Validate();
		}
		catch (ArgumentOutOfRangeException) { invalidSplitRejected = true; }
		if (!invalidSplitRejected)
			throw new Exception("Non-increasing cascade split ratios were accepted.");

		bool zeroAtlasRejected = false;
		try
		{
			new DirectionalShadowCascadeSettings { AtlasWidth = 0 }.Validate();
		}
		catch (ArgumentOutOfRangeException) { zeroAtlasRejected = true; }
		if (!zeroAtlasRejected)
			throw new Exception("A zero-sized shadow atlas was accepted.");

		bool overBudgetAtlasRejected = false;
		try
		{
			new DirectionalShadowCascadeSettings
			{
				CascadeCount = 4,
				SplitFractions = new[] { 0.1f, 0.3f, 0.6f },
				AtlasWidth = 16384,
				AtlasHeight = 16384
			}.Validate();
		}
		catch (ArgumentOutOfRangeException) { overBudgetAtlasRejected = true; }
		if (!overBudgetAtlasRejected)
			throw new Exception("A shadow atlas exceeding the render-target budget was accepted.");
	}

	private static void TestMainLightSelection()
	{
		static VisibleLight Directional(string identity, float intensity, bool castsShadows = true) => new(new DirectionalLightValue
		{
			Identity = identity,
			Intensity = intensity,
			Direction = new Vector3(0, -1, 0),
			CastsShadows = castsShadows,
			ShadowStrength = 1,
			ShadowBias = 0.001f
		});
		static VisibleLight Point(string identity, float intensity) => new(new PointLightValue
		{
			Identity = identity,
			Intensity = intensity,
			Range = 5
		});

		VisibleLight[] lights = { Directional("zeta", 2), Point("point", 100), Directional("alpha", 2), Directional("weak", 1) };
		if (ScriptableRenderer.SelectMainDirectionalLight(lights) != 2)
			throw new Exception("Main directional light selection did not use intensity then stable identity ordering.");
		if (ScriptableRenderer.SelectMainDirectionalLight(new[] { Point("point", 100) }) != -1)
			throw new Exception("A point light was selected as the main directional light.");
		VisibleLight[] disabledMain = { Directional("strong-no-shadow", 3, false), Directional("weak-shadow", 2) };
		int selected = ScriptableRenderer.SelectMainDirectionalLight(disabledMain);
		if (selected != 0 || disabledMain[selected].CastsShadows ||
		    ScriptableRenderer.ShouldRenderMainLightShadows(true, disabledMain, selected))
			throw new Exception("A weaker shadow-casting light replaced the selected non-shadowing main light.");
		if (ScriptableRenderer.ShouldRenderMainLightShadows(true, lights, 2) == false ||
		    ScriptableRenderer.ShouldRenderMainLightShadows(false, lights, 2) ||
		    ScriptableRenderer.ShouldRenderMainLightShadows(true, lights, -1))
			throw new Exception("Main light shadow activation did not honor its enable, selection and cast state.");
	}

	private static void Expect(bool expected, RenderPassEvent passEvent, ScriptableRenderPassInput input, string message)
	{
		if (ScriptableRenderPass.AreInputsAvailableAt(passEvent, input) != expected)
			throw new Exception(message);
	}
}
