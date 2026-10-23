using System;
using System.Collections.Generic;
using Ursa;
using Ursa.Rendering;

namespace Ursa.SessionFixtures;

public sealed class UniversalPipelineProbe : Behaviour
{
	public CameraComponent? depthCamera = null;
	public CameraComponent? nothingCamera = null;
	public string expectedDirectionalIdentity = string.Empty;
	public string expectedPointIdentity = string.Empty;
	private readonly List<Camera> cameras = new();
	private UniversalRenderPipelineAsset? pipelineAsset;
	private UniversalRendererData? rendererData;

	public override void Update()
	{
		ValidateCameraAndLightSnapshot();
		if (pipelineAsset == null)
		{
			rendererData = ScriptableObject.CreateInstance<UniversalRendererData>();
			pipelineAsset = ScriptableObject.CreateInstance<UniversalRenderPipelineAsset>();
			pipelineAsset.RendererData = rendererData;
			pipelineAsset.MainLightShadowsEnabled = false;
			pipelineAsset.ToneMapping = ToneMappingMode.Reinhard;
			RenderPipelineManager.SetPipelineAsset(pipelineAsset);
		}
		cameras.Clear();
		cameras.Add(Camera.FromComponent(GameObject.GetComponent<CameraComponent>()!));
		if (depthCamera != null) cameras.Add(Camera.FromComponent(depthCamera));
		if (nothingCamera != null) cameras.Add(Camera.FromComponent(nothingCamera));
		RenderPipelineManager.Render(cameras, 1);
		GameObject.Name = "Universal:Rendered";
	}

	private void ValidateCameraAndLightSnapshot()
	{
		Camera camera = Camera.FromComponent(GameObject.GetComponent<CameraComponent>()!);
		CullingResults culling;
		var context = new ScriptableRenderContext(1);
		using (context)
		{
			context.SetupCameraProperties(camera);
			if (!camera.Orthographic || Math.Abs(camera.NearPlane - 0.25f) > 1e-5f ||
			    Math.Abs(camera.FarPlane - 42) > 1e-5f)
				throw new InvalidOperationException("Camera snapshot lost projection type or clipping planes.");
			var identity = camera.ProjectionMatrix * camera.ViewMatrix * camera.InverseViewProjectionMatrix;
			for (int index = 0; index < 4; ++index)
				if (Math.Abs(identity[index, index] - 1) > 1e-4f)
					throw new InvalidOperationException("Camera snapshot inverse view-projection does not match its matrices.");
			culling = context.Cull(camera);
			VisibleLight[] lights = context.GetVisibleLights(culling);
			VisibleLight? directional = null;
			VisibleLight? point = null;
			foreach (VisibleLight light in lights)
			{
				if (light.Identity == expectedDirectionalIdentity) directional = light;
				if (light.Identity == expectedPointIdentity) point = light;
			}
			if (lights.Length != 2 || directional is not VisibleLight main || point is not VisibleLight local ||
			    main.Type != VisibleLightType.Directional || !main.CastsShadows || Math.Abs(main.Intensity - 3.5f) > 1e-5f ||
			    Math.Abs(main.ShadowStrength - 0.6f) > 1e-5f || Math.Abs(main.ShadowBias - 0.015f) > 1e-5f ||
			    Math.Abs(main.ShadowNormalBias - 0.08f) > 1e-5f || !main.SoftShadows ||
			    local.Type != VisibleLightType.Point || Math.Abs(local.Intensity - 2.25f) > 1e-5f ||
			    Math.Abs(local.Range - 4) > 1e-5f)
				throw new InvalidOperationException("Managed visible-light batch differs from the native scene snapshot.");
		}
		bool expiredRejected = false;
		try { _ = context.GetVisibleLights(culling); }
		catch (ObjectDisposedException) { expiredRejected = true; }
		if (!expiredRejected)
			throw new InvalidOperationException("Disposed render contexts accepted a stale light query.");
	}
}
