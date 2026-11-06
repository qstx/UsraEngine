using Ursa;
using Ursa.Rendering;

namespace Ursa.Demo.Rendering;

[CreateAssetMenu(MenuName = "Rendering/Features/Screen Tint", FileName = "ScreenTintFeature", Order = 20)]
public sealed class ScreenTintFeatureAsset : ScriptableRendererFeature
{
	public Color tint = new(1, 0.94f, 0.86f, 1);
	public static bool TintEnabled { get; private set; } = true;

	public static void ToggleTint() => TintEnabled = !TintEnabled;

	protected override RendererFeature CreateFeature()
	{
		Material material = Material.Create(new AssetReference("36ec5c8a-265d-47a0-aab5-5cb2cf30fb94"));
		material.Sampler = new MaterialSampler { Linear = true, Repeat = false };
		return new ScreenTintFeature(material, tint);
	}
}

internal sealed class ScreenTintFeature : RendererFeature
{
	private readonly ScreenTintPass pass;

	internal ScreenTintFeature(Material material, Color tint) => pass = new ScreenTintPass(material, tint);

	public override void AddRenderPasses(ScriptableRenderer renderer, Camera camera)
	{
		if (camera.Type != CameraType.Game) return;
		if (ScreenTintFeatureAsset.TintEnabled) renderer.EnqueuePass(pass);
	}

	protected override void Dispose(bool disposing)
	{
		if (disposing) pass.Dispose();
	}
}

internal sealed class ScreenTintPass : ScriptableRenderPass
{
	private readonly Material material;
	private readonly Color tint;

	internal ScreenTintPass(Material material, Color tint)
	{
		this.material = material;
		this.tint = tint;
		RenderPassEvent = RenderPassEvent.AfterTransparents;
		CameraTypeMask = 1u << (int)CameraType.Game;
	}

	public override void Execute(ScriptableRenderContext context, Camera camera, CommandBuffer commands)
	{
		RenderTargetIdentifier color = context.CameraColorTarget;
		using var scope = commands.CreateTemporaryTargetScope();
		RenderTargetIdentifier copy = scope.Allocate(new RenderTextureDescriptor(camera.PixelWidth, camera.PixelHeight,
			GraphicsFormat.R16G16B16A16_SFloat));
		commands.CopyTexture(color, copy);
		commands.SetGlobalVector("_UrsaDemoScreenTint", new Vector4(tint.R, tint.G, tint.B, tint.A));
		commands.Blit(color, copy, material, 0);
		commands.CopyTexture(copy, color);
	}

	internal void Dispose() => material.Destroy();
}
