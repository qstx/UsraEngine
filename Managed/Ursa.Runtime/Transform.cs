namespace Ursa;

public struct Vector3
{
	public float X;
	public float Y;
	public float Z;

	public Vector3(float x, float y, float z)
	{
		X = x;
		Y = y;
		Z = z;
	}
}

public struct Quaternion
{
	public float X;
	public float Y;
	public float Z;
	public float W;

	public Quaternion(float x, float y, float z, float w)
	{
		X = x;
		Y = y;
		Z = z;
		W = w;
	}
}

public struct Color
{
	public float R;
	public float G;
	public float B;
	public float A;

	public Color(float r, float g, float b, float a = 1.0f)
	{
		R = r;
		G = g;
		B = b;
		A = a;
	}
}

public unsafe sealed class Transform
{
	private readonly NativeHandle handle;

	internal Transform(NativeHandle handle)
	{
		this.handle = handle;
	}

	public Vector3 Position
	{
		get => EngineApi.GetWorldPosition(handle);
		set => EngineApi.SetWorldPosition(handle, value);
	}

	public Vector3 LocalPosition
	{
		get
		{
			float* values = stackalloc float[10];
			EngineApi.GetTransform(handle, values);
			return new Vector3(values[0], values[1], values[2]);
		}
		set
		{
			float* values = stackalloc float[10];
			EngineApi.GetTransform(handle, values);
			values[0] = value.X;
			values[1] = value.Y;
			values[2] = value.Z;
			EngineApi.SetTransform(handle, values);
		}
	}

	public Quaternion LocalRotation
	{
		get
		{
			float* values = stackalloc float[10];
			EngineApi.GetTransform(handle, values);
			return new Quaternion(values[3], values[4], values[5], values[6]);
		}
		set
		{
			float* values = stackalloc float[10];
			EngineApi.GetTransform(handle, values);
			values[3] = value.X;
			values[4] = value.Y;
			values[5] = value.Z;
			values[6] = value.W;
			EngineApi.SetTransform(handle, values);
		}
	}

	public Vector3 LocalScale
	{
		get
		{
			float* values = stackalloc float[10];
			EngineApi.GetTransform(handle, values);
			return new Vector3(values[7], values[8], values[9]);
		}
		set
		{
			float* values = stackalloc float[10];
			EngineApi.GetTransform(handle, values);
			values[7] = value.X;
			values[8] = value.Y;
			values[9] = value.Z;
			EngineApi.SetTransform(handle, values);
		}
	}
}
