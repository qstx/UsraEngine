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

	public bool Orthographic
	{
		get => NativeRuntimeBindings.GetCameraProjection(NativeHandle).Orthographic;
		set {
			CameraProjection current = NativeRuntimeBindings.GetCameraProjection(NativeHandle);
			current.Orthographic = value;
			NativeRuntimeBindings.SetCameraProjection(NativeHandle, current);
		}
	}

	public float OrthographicSize
	{
		get => NativeRuntimeBindings.GetCameraProjection(NativeHandle).OrthographicSize;
		set {
			CameraProjection current = NativeRuntimeBindings.GetCameraProjection(NativeHandle);
			current.OrthographicSize = value;
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
	private DirectionalLightShadowSettings ShadowSettings
	{
		get => NativeRuntimeBindings.GetDirectionalLightShadowSettings(NativeHandle);
		set => NativeRuntimeBindings.SetDirectionalLightShadowSettings(NativeHandle, value);
	}

	public bool CastsShadows
	{
		get => ShadowSettings.CastsShadows;
		set { var current = ShadowSettings; current.CastsShadows = value; ShadowSettings = current; }
	}
	public float ShadowStrength
	{
		get => ShadowSettings.Strength;
		set { var current = ShadowSettings; current.Strength = value; ShadowSettings = current; }
	}
	public float ShadowBias
	{
		get => ShadowSettings.Bias;
		set { var current = ShadowSettings; current.Bias = value; ShadowSettings = current; }
	}
	public float ShadowNormalBias
	{
		get => ShadowSettings.NormalBias;
		set { var current = ShadowSettings; current.NormalBias = value; ShadowSettings = current; }
	}
	public bool SoftShadows
	{
		get => ShadowSettings.SoftShadows;
		set { var current = ShadowSettings; current.SoftShadows = value; ShadowSettings = current; }
	}
}

[NativeComponentType("ursa.PointLight")]
public sealed class PointLightComponent : Component
{
}
