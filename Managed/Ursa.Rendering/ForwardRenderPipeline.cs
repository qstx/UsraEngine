using System;
using System.Collections.Generic;
using Ursa;

namespace Ursa.Rendering;

[UrsaScriptType("ursa.rendering.forward", Version = 1)]
[CreateAssetMenu(MenuName = "Rendering/Forward Pipeline", FileName = "ForwardRenderPipeline")]
public sealed class ForwardRenderPipelineAsset : RenderPipelineAsset
{
	protected override RenderPipeline CreatePipeline() => new ForwardRenderPipeline();
}

public sealed class ForwardRenderPipeline : RenderPipeline
{
	private readonly CommandBuffer commands = new("Ursa Forward");
	private static readonly ProfilingSampler clearSampler = new("Clear");
	private static readonly ProfilingSampler opaqueSampler = new("Draw Opaque");
	private static readonly ProfilingSampler transparentSampler = new("Draw Transparent");
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

	public override void Render(ScriptableRenderContext context, List<Ursa.Rendering.Camera> cameras)
	{
		foreach (Ursa.Rendering.Camera camera in cameras)
		{
			commands.Clear();
			bool cameraStarted = false;
			try
			{
				context.SetupCameraProperties(camera);
				cameraStarted = true;
				RenderPipelineManager.BeginCamera(context, camera);
				CullingResults culling = context.Cull(camera);
				RendererList opaque = context.CreateRendererList(culling, opaqueDrawing, opaqueFiltering);
				RendererList transparent = context.CreateRendererList(culling, transparentDrawing, transparentFiltering);
				commands.SetRenderTarget(0);
				Viewport viewport = camera.Viewport;
				commands.SetViewport(viewport.X * camera.PixelWidth, viewport.Y * camera.PixelHeight,
					viewport.Width * camera.PixelWidth, viewport.Height * camera.PixelHeight);
				int left = (int)MathF.Floor(viewport.X * camera.PixelWidth);
				int top = (int)MathF.Floor(viewport.Y * camera.PixelHeight);
				uint right = (uint)MathF.Ceiling((viewport.X + viewport.Width) * camera.PixelWidth);
				uint bottom = (uint)MathF.Ceiling((viewport.Y + viewport.Height) * camera.PixelHeight);
				commands.SetScissor(left, top, right - (uint)left, bottom - (uint)top);
				if (camera.ClearFlags != CameraClearFlags.Nothing)
				{
					Color background = camera.Background;
					using (new ProfilingScope(commands, clearSampler))
						commands.ClearTarget(camera.ClearFlags == CameraClearFlags.ColorAndDepth, true, false,
							new Vector4(background.R, background.G, background.B, background.A));
				}
				using (new ProfilingScope(commands, opaqueSampler))
					commands.DrawRendererList(opaque, context);
				using (new ProfilingScope(commands, transparentSampler))
					commands.DrawRendererList(transparent, context);
				context.ExecuteCommandBuffer(commands);
				context.Submit();
				commands.Clear();
			}
			finally
			{
				commands.Clear();
				if (cameraStarted) RenderPipelineManager.EndCamera(context, camera);
			}
		}
	}

	protected override void Dispose(bool disposing)
	{
		if (disposing) commands.Dispose();
	}
}
