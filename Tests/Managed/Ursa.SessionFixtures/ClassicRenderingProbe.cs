using System;
using System.Collections.Generic;
using Ursa;
using Ursa.Rendering;

namespace Ursa.SessionFixtures;

[UrsaScriptType("test.classic-rendering")]
public sealed class ClassicRenderingProbe : Behaviour
{
	public override void Update()
	{
		using var context = new ScriptableRenderContext(1);
		try
		{
			Camera camera = Camera.FromComponent(GameObject.GetComponent<CameraComponent>()!);
			context.SetupCameraProperties(camera);
			Ursa.Examples.ClassicRendering.DrawCamera(context, GameObject.GetComponent<CameraComponent>()!, 64, 64);
			GameObject.Name = $"ClassicRendering:{camera.OutputTargetIdentity:x16}:{camera.OutputTargetGeneration:x16}";
		}
		catch
		{
			context.Abort();
			throw;
		}
	}
}

[UrsaScriptType("test.rendering-failure")]
public sealed class RenderingFailureProbe : Behaviour
{
	private sealed class FailingAsset : RenderPipelineAsset
	{
		protected override RenderPipeline CreatePipeline() => new FailingPipeline();
	}

	private sealed class FailingPipeline : RenderPipeline
	{
		public override void Render(ScriptableRenderContext context, List<Ursa.Rendering.Camera> cameras)
		{
			context.SetupCameraProperties(cameras[0]);
			using var commands = new CommandBuffer("Failure segment probe");
			commands.ClearTarget(true, true, false, new Vector4(0, 1, 0, 1));
			context.ExecuteCommandBuffer(commands);
			context.Submit();
			commands.Clear();
			commands.ClearTarget(true, true, false, new Vector4(1, 0, 0, 1));
			context.ExecuteCommandBuffer(commands);
			throw new InvalidOperationException("render failure probe");
		}
	}

	public override void Update()
	{
		using var asset = ScriptableObject.CreateInstance<FailingAsset>();
		RenderPipelineManager.SetPipelineAsset(asset);
		RenderPipelineManager.EndContextRendering += _ => throw new InvalidOperationException("end failure probe");
		try
		{
			RenderPipelineManager.Render(new List<Ursa.Rendering.Camera> { GameObject.GetComponent<CameraComponent>()! }, 1);
			throw new InvalidOperationException("The failing pipeline was accepted.");
		}
		catch (AggregateException error) when (error.InnerExceptions.Count == 2 &&
		    error.InnerExceptions[0].Message == "render failure probe" &&
		    error.InnerExceptions[1].Message == "end failure probe")
		{
			GameObject.Name = "RenderingFailure:Aborted";
		}
		finally { RenderPipelineManager.Reset(); }
	}
}

// Deliberately leaves its pipeline and event registered for session-owned cleanup.
[UrsaScriptType("test.pipeline-lifetime")]
public sealed class PipelineLifetimeProbe : Behaviour
{
	private sealed class LifetimeAsset : RenderPipelineAsset
	{
		protected override RenderPipeline CreatePipeline() => new LifetimePipeline(this);
	}

	private sealed class LifetimePipeline : RenderPipeline
	{
		private readonly LifetimeAsset asset;
		private readonly RenderTexture target = RenderTexture.Create(new RenderTextureDescriptor(8, 8));
		internal LifetimePipeline(LifetimeAsset asset) => this.asset = asset;
		public override void Render(ScriptableRenderContext context, List<Ursa.Rendering.Camera> cameras)
		{
			context.SetupCameraProperties(cameras[0]);
			using var commands = new CommandBuffer("Session pipeline");
			commands.ClearTarget(true, true, false, new Vector4(0, 0, 1, 1));
			context.ExecuteCommandBuffer(commands);
			context.Submit();
		}
		protected override void Dispose(bool disposing)
		{
			if (!asset.IsValid) throw new InvalidOperationException("Pipeline was disposed after its asset session.");
			target.Dispose();
			Debug.Log("Session pipeline disposed before asset cleanup");
		}
	}

	private LifetimeAsset? asset;
	private List<Ursa.Rendering.Camera>? cameras;
	private int contextEvents;
	public override void Awake()
	{
		asset = ScriptableObject.CreateInstance<LifetimeAsset>();
		cameras = new List<Ursa.Rendering.Camera> { GameObject.GetComponent<CameraComponent>()! };
		RenderPipelineManager.SetPipelineAsset(asset);
		RenderPipelineManager.BeginContextRendering += OnBeginContext;
	}
	private void OnBeginContext(ScriptableRenderContext context) => ++contextEvents;
	public override void Update()
	{
		RenderPipelineManager.Render(cameras!, 1);
		GameObject.Name = $"PipelineLifetime:{contextEvents}";
	}
}
