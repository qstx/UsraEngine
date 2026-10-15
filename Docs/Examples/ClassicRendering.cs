using Ursa;
using Ursa.Rendering;

namespace Ursa.Examples;

public static class ClassicRendering
{
	// width/height must match the host camera output for CopyTexture.
	public static void DrawCamera(ScriptableRenderContext context, CameraComponent camera, uint width, uint height)
	{
		context.SetupCameraProperties(camera);
		CullingResults culling = context.Cull(camera);
		string[] tags = { "SRPDefaultUnlit", "UniversalForward", "ForwardBase" };
		RendererList opaque = context.CreateRendererList(culling,
			new DrawingSettings { ShaderTags = tags, Sorting = SortingCriteria.CommonOpaque },
			new FilteringSettings { MaximumQueue = 2500 });
		RendererList transparent = context.CreateRendererList(culling,
			new DrawingSettings { ShaderTags = tags, Sorting = SortingCriteria.CommonTransparent },
			new FilteringSettings { MinimumQueue = 2501 });

		using var commands = new CommandBuffer("Classic offscreen camera");
		const uint sceneColor = 1;
		commands.GetTemporaryTarget(sceneColor, new RenderTextureDescriptor(width, height, depthBufferBits: 32));
		commands.SetRenderTarget(sceneColor);
		commands.ClearTarget(true, true, false, new Vector4(0, 0, 1, 1));
		commands.DrawRendererList(opaque, context);
		commands.DrawRendererList(transparent, context);
		context.ExecuteCommandBuffer(commands);
		context.Submit();
		commands.Clear();

		commands.CopyTexture((RenderTargetIdentifier)sceneColor, RenderTargetIdentifier.CameraTarget);
		commands.ReleaseTemporaryTarget(sceneColor);
		context.ExecuteCommandBuffer(commands);
		context.Submit();
		commands.Clear();
	}
}
