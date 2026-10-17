using System;
using Ursa.Bindings;

namespace Ursa;

[WireValue]
public struct Vector2
{
	public float X;
	public float Y;
	public Vector2(float x, float y) { X = x; Y = y; }
}

[WireValue]
public struct Vector3
{
	public float X;
	public float Y;
	public float Z;
	public Vector3(float x, float y, float z) { X = x; Y = y; Z = z; }
}

[WireValue]
public struct Vector4
{
	public float X;
	public float Y;
	public float Z;
	public float W;
	public Vector4(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }
}

[WireValue]
public struct Quaternion
{
	public float X;
	public float Y;
	public float Z;
	public float W;
	public Quaternion(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }
}

[WireValue]
public struct Color
{
	public float R;
	public float G;
	public float B;
	public float A;
	public Color(float r, float g, float b, float a = 1.0f) { R = r; G = g; B = b; A = a; }
	public Color ToLinear() => new(MathF.Pow(MathF.Max(R, 0), 2.2f), MathF.Pow(MathF.Max(G, 0), 2.2f), MathF.Pow(MathF.Max(B, 0), 2.2f), A);
}

// Column-major storage, column-vector multiplication, matching the native Mat4 contract.
[WireValue]
public struct Matrix4x4
{
	public float M00;
	public float M10;
	public float M20;
	public float M30;
	public float M01;
	public float M11;
	public float M21;
	public float M31;
	public float M02;
	public float M12;
	public float M22;
	public float M32;
	public float M03;
	public float M13;
	public float M23;
	public float M33;

	public Matrix4x4(ReadOnlySpan<float> columnMajorValues)
	{
		if (columnMajorValues.Length != 16) throw new ArgumentException("A matrix requires sixteen column-major values.", nameof(columnMajorValues));
		this = default;
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row) this[row, column] = columnMajorValues[column * 4 + row];
	}

	public static Matrix4x4 Identity => new() { M00 = 1, M11 = 1, M22 = 1, M33 = 1 };

	public float this[int row, int column]
	{
		readonly get
		{
			if ((uint)row >= 4 || (uint)column >= 4) throw new ArgumentOutOfRangeException(nameof(row));
			return (column * 4 + row) switch
			{
				0 => M00, 1 => M10, 2 => M20, 3 => M30,
				4 => M01, 5 => M11, 6 => M21, 7 => M31,
				8 => M02, 9 => M12, 10 => M22, 11 => M32,
				12 => M03, 13 => M13, 14 => M23, 15 => M33,
				_ => throw new ArgumentOutOfRangeException(nameof(row))
			};
		}
		set
		{
			if ((uint)row >= 4 || (uint)column >= 4) throw new ArgumentOutOfRangeException(nameof(row));
			switch (column * 4 + row)
			{
				case 0: M00 = value; break; case 1: M10 = value; break; case 2: M20 = value; break; case 3: M30 = value; break;
				case 4: M01 = value; break; case 5: M11 = value; break; case 6: M21 = value; break; case 7: M31 = value; break;
				case 8: M02 = value; break; case 9: M12 = value; break; case 10: M22 = value; break; case 11: M32 = value; break;
				case 12: M03 = value; break; case 13: M13 = value; break; case 14: M23 = value; break; case 15: M33 = value; break;
			}
		}
	}

	public static Matrix4x4 Translation(Vector3 position)
	{
		Matrix4x4 result = Identity;
		result.M03 = position.X;
		result.M13 = position.Y;
		result.M23 = position.Z;
		return result;
	}

	public static Matrix4x4 operator *(Matrix4x4 left, Matrix4x4 right)
	{
		Matrix4x4 result = default;
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row)
			{
				float sum = 0;
				for (int inner = 0; inner < 4; ++inner) sum += left[row, inner] * right[inner, column];
				result[row, column] = sum;
			}
		return result;
	}

	public readonly Vector3 MultiplyPoint3x4(Vector3 value) => new(
		M00 * value.X + M01 * value.Y + M02 * value.Z + M03,
		M10 * value.X + M11 * value.Y + M12 * value.Z + M13,
		M20 * value.X + M21 * value.Y + M22 * value.Z + M23);
}
