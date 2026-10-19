using System;
using Ursa;

namespace Ursa.Demo.Rendering;

// Attach to the existing Game camera to compare two independent half-window views in Play.
public sealed class MultiCameraShowcase : Behaviour
{
	private GameObject? secondary;
	private CameraComponent? primary;
	private CameraRenderingSettings? original;

	public override void Awake()
	{
		primary = GameObject.GetComponent<CameraComponent>() ??
			throw new InvalidOperationException("Attach MultiCameraShowcase to a Game camera.");
		original = primary.RenderingSettings;
		primary.RenderingSettings = Settings(new Viewport(0, 0, 0.5f, 1), original.Depth);
		secondary = GameObject.Create("Pipeline second camera");
		secondary.Parent = GameObject.Parent;
		Vector3 position = Transform.LocalPosition;
		position.X += 1;
		secondary.Transform.LocalPosition = position;
		secondary.Transform.LocalRotation = Transform.LocalRotation;
		CameraComponent camera = secondary.AddComponent<CameraComponent>();
		camera.VerticalFovRadians = primary.VerticalFovRadians;
		camera.NearPlane = primary.NearPlane;
		camera.FarPlane = primary.FarPlane;
		camera.RenderingSettings = Settings(new Viewport(0.5f, 0, 0.5f, 1), original.Depth + 1);
	}

	private CameraRenderingSettings Settings(Viewport viewport, float depth) => new()
	{
		Type = CameraType.Game,
		Depth = depth,
		Viewport = viewport,
		Background = original!.Background,
		ClearFlags = CameraClearFlags.ColorAndDepth,
		CullingMask = original.CullingMask
	};

	public override void OnDestroy()
	{
		secondary?.Destroy();
		secondary = null;
		if (primary != null && original != null) primary.RenderingSettings = original;
	}
}
