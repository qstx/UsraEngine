using System;
using System.Collections.Generic;
using Ursa;
using Ursa.Rendering;

namespace Ursa.SessionFixtures;

public sealed class ReloadDependency : ScriptableObject
{
	public int value = 7;
	public ReloadDependency? self;
}

public sealed class ReloadPipelineAsset : RenderPipelineAsset
{
	public int tint = 23;
	public bool failFactory;
	public ReloadDependency? shared;
	protected override void OnValidate()
	{
		if (tint < 0) throw new InvalidOperationException("Invalid reload tint.");
	}
	protected override RenderPipeline CreatePipeline()
	{
		if (failFactory) throw new InvalidOperationException("Reload factory failed.");
		return new ReloadPipeline(tint);
	}

	private sealed class ReloadPipeline : RenderPipeline
	{
		private readonly int tint;
		internal ReloadPipeline(int tint) => this.tint = tint;
		public override void Render(ScriptableRenderContext context, List<Ursa.Rendering.Camera> cameras)
		{
			context.SetupCameraProperties(cameras[0]);
			using var commands = new CommandBuffer("Reload tint");
			commands.ClearTarget(true, true, false, new Vector4(tint / 255f, 0, 0, 1));
			context.ExecuteCommandBuffer(commands);
			context.Submit();
		}
	}
}

public sealed class PipelineReloadProbe : Behaviour
{
	public int action;
	public override void Update()
	{
		if (action == 5)
		{
			using var presentation = ScriptableObject.CreateInstance<Ursa.Examples.PresentationPipelineAsset>();
			presentation.clearColor = new Color(0.1f, 0.3f, 0.7f, 1);
			try
			{
				RenderPipelineManager.SetPipelineAsset(presentation);
				RenderPipelineManager.Render(new List<Camera> { Camera.FromComponent(GameObject.GetComponent<CameraComponent>()!) }, 1);
				GameObject.Name = "PipelineReload:Presentation";
			}
			finally { RenderPipelineManager.Reset(); }
			return;
		}
		var asset = ScriptableObject.Load<ReloadPipelineAsset>(
			new AssetReference("a5137701-cc10-40f1-8420-8e9769daa201", 1));
		if (action == 0)
		{
			asset.tint = 41;
			asset.shared!.value = 19;
			RenderPipelineManager.SetPipelineAsset(asset);
		}
		else if (action == 2) { asset.failFactory = true; return; }
		else if (action == 3) { asset.failFactory = false; asset.tint = -1; return; }
		else if (action == 4) { asset.tint = 41; asset.failFactory = false; }
		if (asset.tint != 41 || asset.shared?.value != 19 || !ReferenceEquals(asset.shared.self, asset.shared))
			throw new InvalidOperationException("Reload lost fields or cyclic shared asset identity.");
		RenderPipelineManager.Render(new List<Ursa.Rendering.Camera> { GameObject.GetComponent<CameraComponent>()! }, 1);
		if (!ReferenceEquals(RenderPipelineManager.ActiveAsset, asset))
			throw new InvalidOperationException("Reload did not restore the active pipeline selection.");
		GameObject.Name = "PipelineReload:41:19";
	}
}
