using System.Collections.Generic;
using Ursa;

namespace Ursa.Rendering;

[CreateAssetMenu(MenuName = "Rendering/Forward Pipeline", FileName = "ForwardRenderPipeline")]
public sealed class ForwardRenderPipelineAsset : RenderPipelineAsset
{
	protected override RenderPipeline CreatePipeline() => new ForwardRenderPipeline();
}

public sealed class ForwardRenderPipeline : RenderPipeline
{
	private readonly ScriptableRenderer renderer = new(false, new DirectionalShadowCascadeSettings(), 0,
		ToneMappingMode.Reinhard, false);
	private static readonly RendererFeature[] noFeatures = System.Array.Empty<RendererFeature>();

	public override void Render(ScriptableRenderContext context, List<Ursa.Rendering.Camera> cameras)
	{
		foreach (Camera camera in cameras) renderer.RenderCamera(context, camera, noFeatures);
	}

	protected override void Dispose(bool disposing)
	{
		if (disposing) renderer.Dispose();
	}
}
