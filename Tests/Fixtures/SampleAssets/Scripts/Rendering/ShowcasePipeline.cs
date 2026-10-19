using System;
using System.Collections.Generic;
using Ursa;
using Ursa.Rendering;
using Camera = Ursa.Rendering.Camera;

namespace Ursa.Demo.Rendering;
[CreateAssetMenu(MenuName = "Rendering/Demo Showcase Pipeline", FileName = "ShowcasePipeline", Order = 10)]
public sealed class ShowcasePipelineAsset : RenderPipelineAsset
{
	public Material? compositeMaterial;
	public Vector4 tint = new(0.9f, 1.0f, 0.85f, 1.0f);
	public ComputeShader? patternShader;
	public Material? replacementMaterial;
	public float patternStrength;

	protected override RenderPipeline CreatePipeline() => new ShowcasePipeline(
		compositeMaterial ?? throw new InvalidOperationException("Select the Demo Composite material before applying this pipeline."),
		tint, patternShader, replacementMaterial, patternStrength);
}

public sealed class ShowcasePipeline : RenderPipeline
{
	private readonly CommandBuffer commands = new("Demo offscreen and composite");
	private readonly Dictionary<Camera, Target> targets = new();
	private readonly Material composite;
	private readonly Vector4 tint;
	private readonly ComputeShader? patternShader;
	private readonly RenderTexture? pattern;
	private readonly int patternKernel;
	private readonly uint patternGroupsX;
	private readonly uint patternGroupsY;
	private readonly float patternStrength;
	private readonly DrawingSettings opaqueDrawing = new()
	{
		ShaderTags = new[] { "SRPDefaultUnlit", "UniversalForward", "ForwardBase" },
		Sorting = SortingCriteria.CommonOpaque
	};
	private readonly DrawingSettings transparentDrawing = new()
	{
		ShaderTags = new[] { "SRPDefaultUnlit", "UniversalForward", "ForwardBase" },
		Sorting = SortingCriteria.CommonTransparent
	};
	private readonly FilteringSettings opaqueFiltering = new() { MaximumQueue = 2500 };
	private readonly FilteringSettings transparentFiltering = new() { MinimumQueue = 2501 };

	private sealed record Target(uint Width, uint Height, RenderTexture Texture);

	public ShowcasePipeline(Material composite, Vector4 tint, ComputeShader? patternShader,
		Material? replacementMaterial, float patternStrength)
	{
		if (!float.IsFinite(patternStrength) || patternStrength < 0 || patternStrength > 1)
			throw new ArgumentOutOfRangeException(nameof(patternStrength));
		if (patternStrength > 0 && patternShader == null)
			throw new InvalidOperationException("A nonzero pattern strength requires the Demo Pattern compute shader.");
		this.composite = composite;
		this.tint = tint;
		this.patternShader = patternShader;
		this.patternStrength = patternStrength;
		opaqueDrawing.OverrideMaterial = replacementMaterial;
		if (patternShader != null)
		{
			patternKernel = patternShader.FindKernel("GeneratePattern");
			var threads = patternShader.GetKernelThreadGroupSizes(patternKernel);
			if (threads.Z != 1) throw new InvalidOperationException("Pattern kernel must use a two-dimensional thread group.");
			patternGroupsX = (64 + threads.X - 1) / threads.X;
			patternGroupsY = (64 + threads.Y - 1) / threads.Y;
			pattern = RenderTexture.Create(new RenderTextureDescriptor(64, 64, enableRandomWrite: true));
		}
	}

	public override void Render(ScriptableRenderContext context, List<Camera> cameras)
	{
		if (patternShader != null && pattern != null)
		{
			commands.SetComputeTexture(patternShader, patternKernel, "_Output", pattern);
			commands.DispatchCompute(patternShader, patternKernel, patternGroupsX, patternGroupsY);
			Execute(context);
		}
		foreach (Camera camera in cameras)
		{
			context.SetupCameraProperties(camera);
			RenderPipelineManager.BeginCamera(context, camera);
			try
			{
				// Both scene stages use the same snapshot and exactly one culling result.
				CullingResults culling = context.Cull(camera);
				RendererList opaque = context.CreateRendererList(culling, opaqueDrawing, opaqueFiltering);
				RendererList transparent = context.CreateRendererList(culling, transparentDrawing, transparentFiltering);
				RenderTexture sceneColor = GetTarget(camera);
				commands.SetRenderTarget(sceneColor);
				commands.SetViewport(0, 0, camera.PixelWidth, camera.PixelHeight);
				commands.SetScissor(0, 0, camera.PixelWidth, camera.PixelHeight);
				Color background = camera.Background;
				commands.ClearTarget(true, true, false, new Vector4(background.R, background.G, background.B, background.A));
				commands.DrawRendererList(opaque, context);
				Execute(context);
				commands.DrawRendererList(transparent, context);
				Execute(context);

				// The project shader samples offscreen color and adds a visible green frame.
				commands.SetRenderTarget(RenderTargetIdentifier.CameraTarget);
				Viewport viewport = camera.Viewport;
				commands.SetViewport(viewport.X * camera.PixelWidth, viewport.Y * camera.PixelHeight,
					viewport.Width * camera.PixelWidth, viewport.Height * camera.PixelHeight);
				uint left = (uint)MathF.Floor(viewport.X * camera.PixelWidth);
				uint top = (uint)MathF.Floor(viewport.Y * camera.PixelHeight);
				uint right = (uint)MathF.Ceiling((viewport.X + viewport.Width) * camera.PixelWidth);
				uint bottom = (uint)MathF.Ceiling((viewport.Y + viewport.Height) * camera.PixelHeight);
				commands.SetScissor((int)left, (int)top, right - left, bottom - top);
				commands.SetGlobalTexture("_Source", sceneColor);
				commands.SetGlobalVector("_DemoTint", tint);
				commands.SetGlobalTexture("_Pattern", pattern ?? sceneColor);
				commands.SetGlobalFloat("_DemoPatternStrength", patternStrength);
				commands.DrawFullscreen(composite, 0);
				Execute(context);
			}
			finally
			{
				commands.Clear();
				RenderPipelineManager.EndCamera(context, camera);
			}
		}
	}

	private RenderTexture GetTarget(Camera camera)
	{
		if (targets.TryGetValue(camera, out Target? previous))
		{
			if (previous.Width == camera.PixelWidth && previous.Height == camera.PixelHeight) return previous.Texture;
			previous.Texture.Dispose();
		}
		RenderTexture texture = RenderTexture.Create(new RenderTextureDescriptor(
			camera.PixelWidth, camera.PixelHeight, depthBufferBits: 32));
		targets[camera] = new Target(camera.PixelWidth, camera.PixelHeight, texture);
		return texture;
	}

	private void Execute(ScriptableRenderContext context)
	{
		context.ExecuteCommandBuffer(commands);
		context.Submit();
		commands.Clear();
	}

	protected override void Dispose(bool disposing)
	{
		if (!disposing) return;
		commands.Dispose();
		pattern?.Dispose();
		foreach (Target target in targets.Values) target.Texture.Dispose();
		targets.Clear();
	}
}
