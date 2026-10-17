namespace Ursa;

[NativeComponentType("ursa.Camera")]
public sealed class CameraComponent : Component
{
	public CameraRenderingSettings RenderingSettings
	{
		get => NativeRuntimeBindings.GetCameraRenderingSettings(NativeHandle);
		set
		{
			System.ArgumentNullException.ThrowIfNull(value);
			NativeRuntimeBindings.SetCameraRenderingSettings(NativeHandle, value);
		}
	}

	public float VerticalFovRadians
	{
		get => NativeRuntimeBindings.GetCameraProjection(NativeHandle).VerticalFovRadians;
		set {
			CameraProjection current = NativeRuntimeBindings.GetCameraProjection(NativeHandle);
			current.VerticalFovRadians = value;
			NativeRuntimeBindings.SetCameraProjection(NativeHandle, current);
		}
	}

	public float NearPlane
	{
		get => NativeRuntimeBindings.GetCameraProjection(NativeHandle).NearPlane;
		set {
			CameraProjection current = NativeRuntimeBindings.GetCameraProjection(NativeHandle);
			current.NearPlane = value;
			NativeRuntimeBindings.SetCameraProjection(NativeHandle, current);
		}
	}

	public float FarPlane
	{
		get => NativeRuntimeBindings.GetCameraProjection(NativeHandle).FarPlane;
		set {
			CameraProjection current = NativeRuntimeBindings.GetCameraProjection(NativeHandle);
			current.FarPlane = value;
			NativeRuntimeBindings.SetCameraProjection(NativeHandle, current);
		}
	}
}

[NativeComponentType("ursa.Animator")]
public sealed class AnimatorComponent : Component
{
}

[NativeComponentType("ursa.DirectionalLight")]
public sealed class DirectionalLightComponent : Component
{
}

[NativeComponentType("ursa.PointLight")]
public sealed class PointLightComponent : Component
{
}
