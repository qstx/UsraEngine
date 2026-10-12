namespace Ursa;

[NativeComponentType("ursa.Camera")]
public sealed unsafe class CameraComponent : Component
{
	public float VerticalFovRadians
	{
		get {
			float* values = stackalloc float[3];
			EngineApi.GetCameraProjection(NativeHandle, values);
			return values[0];
		}
		set {
			float* values = stackalloc float[3];
			EngineApi.GetCameraProjection(NativeHandle, values);
			values[0] = value;
			EngineApi.SetCameraProjection(NativeHandle, values);
		}
	}

	public float NearPlane
	{
		get {
			float* values = stackalloc float[3];
			EngineApi.GetCameraProjection(NativeHandle, values);
			return values[1];
		}
		set {
			float* values = stackalloc float[3];
			EngineApi.GetCameraProjection(NativeHandle, values);
			values[1] = value;
			EngineApi.SetCameraProjection(NativeHandle, values);
		}
	}

	public float FarPlane
	{
		get {
			float* values = stackalloc float[3];
			EngineApi.GetCameraProjection(NativeHandle, values);
			return values[2];
		}
		set {
			float* values = stackalloc float[3];
			EngineApi.GetCameraProjection(NativeHandle, values);
			values[2] = value;
			EngineApi.SetCameraProjection(NativeHandle, values);
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
