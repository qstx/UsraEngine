using Ursa.Bindings;

namespace Ursa;

[WireValue]
internal struct LocalTransformValue
{
	public Vector3 Position;
	public Quaternion Rotation;
	public Vector3 Scale;
}

[WireValue]
internal struct CameraProjection
{
	public float VerticalFovRadians;
	public float NearPlane;
	public float FarPlane;
}

[WireValue]
internal struct TimeState
{
	public float DeltaTime;
	public float FixedDeltaTime;
	public double ElapsedTime;
	public double FixedElapsedTime;
	public ulong FrameCount;
}

[WireValue]
internal struct NativeAssetReference
{
	public string Guid;
	public ulong LocalFileId;
	internal static NativeAssetReference From(AssetReference value) => new() { Guid = value.Guid, LocalFileId = value.LocalFileId };
	internal AssetReference ToManaged() => new(Guid, LocalFileId);
}

[WireValue]
internal struct MaterialParametersValue
{
	public Color BaseColor;
	public Color Emissive;
	public float Metallic;
	public float Roughness;
	public float NormalScale;
	public float OcclusionStrength;
	public float AlphaCutoff;
	public AlphaMode AlphaMode;
}

[WireValue]
internal struct ComputeThreadGroupSize
{
	public uint X;
	public uint Y;
	public uint Z;
}
