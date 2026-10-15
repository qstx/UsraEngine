using System.Collections.Generic;
using Ursa;
using Ursa.Rendering;

namespace Ursa.Examples;

[UrsaScriptType("examples.presentation-pipeline")]
[CreateAssetMenu(MenuName = "Rendering/Presentation Pipeline", FileName = "PresentationPipeline")]
public sealed class PresentationPipelineAsset : RenderPipelineAsset
{
	public Color clearColor = new(0.05f, 0.1f, 0.25f, 1);
	protected override RenderPipeline CreatePipeline() => new PresentationPipeline(clearColor);

	private sealed class PresentationPipeline(Color color) : RenderPipeline
	{
		private readonly CommandBuffer commands = new("Presentation Pipeline");

		public override void Render(ScriptableRenderContext context, List<Camera> cameras)
		{
			foreach (Camera camera in cameras)
			{
				RenderPipelineManager.BeginCamera(context, camera);
				try
				{
					context.SetupCameraProperties(camera);
					Viewport viewport = camera.Viewport;
					commands.SetRenderTarget(RenderTargetIdentifier.CameraTarget);
					commands.SetViewport(viewport.X * camera.PixelWidth, viewport.Y * camera.PixelHeight,
						viewport.Width * camera.PixelWidth, viewport.Height * camera.PixelHeight);
					int x = (int)System.MathF.Floor(viewport.X * camera.PixelWidth);
					int y = (int)System.MathF.Floor(viewport.Y * camera.PixelHeight);
					uint right = (uint)System.MathF.Ceiling((viewport.X + viewport.Width) * camera.PixelWidth);
					uint bottom = (uint)System.MathF.Ceiling((viewport.Y + viewport.Height) * camera.PixelHeight);
					commands.SetScissor(x, y, right - (uint)x, bottom - (uint)y);
					if (camera.ClearFlags != CameraClearFlags.Nothing)
						commands.ClearTarget(camera.ClearFlags == CameraClearFlags.ColorAndDepth, true, false,
							new Vector4(color.R, color.G, color.B, color.A));
					context.ExecuteCommandBuffer(commands);
					context.Submit();
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
}
