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

	public override void Render(ScriptableRenderContext context, List<CameraComponent> cameras)
	{
		foreach (CameraComponent camera in cameras)
		{
			commands.Clear();
			RenderPipelineManager.BeginCamera(context, camera);
			try
			{
				context.SetupCameraProperties(camera);
				CullingResults culling = context.Cull(camera);
				var opaqueDrawing = new DrawingSettings
				{
					ShaderTags = new[] { "SRPDefaultUnlit", "UniversalForward", "ForwardBase" },
					Sorting = SortingCriteria.CommonOpaque
				};
				var transparentDrawing = new DrawingSettings
				{
					ShaderTags = opaqueDrawing.ShaderTags,
					Sorting = SortingCriteria.CommonTransparent
				};
				var opaqueFiltering = new FilteringSettings { MinimumQueue = 0, MaximumQueue = 2500 };
				var transparentFiltering = new FilteringSettings { MinimumQueue = 2501, MaximumQueue = 5000 };
				RendererList opaque = context.CreateRendererList(culling, opaqueDrawing, opaqueFiltering);
				RendererList transparent = context.CreateRendererList(culling, transparentDrawing, transparentFiltering);
				commands.SetRenderTarget(0);
				commands.ClearTarget(true, true, false, new Vector4(0.05f, 0.05f, 0.05f, 1));
				commands.DrawRendererList(opaque, context);
				commands.DrawRendererList(transparent, context);
				commands.Execute(context);
				commands.Clear();
			}
			finally
			{
				commands.Clear();
				RenderPipelineManager.EndCamera(context, camera);
			}
		}
	}

	protected override void Dispose(bool disposing)
	{
		if (disposing) commands.Dispose();
	}
}
