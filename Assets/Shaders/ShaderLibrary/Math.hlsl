#ifndef URSA_SHADER_LIBRARY_MATH_INCLUDED
#define URSA_SHADER_LIBRARY_MATH_INCLUDED

#define URSA_PI 3.14159265358979323846
#define URSA_INV_PI 0.31830988618379067154

float UrsaSquare(float value)
{
	return value * value;
}

float3 UrsaSafeNormalize(float3 value, float3 fallbackValue)
{
	float lengthSquared = dot(value, value);
	return lengthSquared > 1.0e-12 ? value * rsqrt(lengthSquared) : fallbackValue;
}

float UrsaSafeRcp(float value, float fallbackValue)
{
	return abs(value) > 1.0e-8 ? rcp(value) : fallbackValue;
}

#endif
