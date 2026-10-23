#ifndef URSA_SHADER_LIBRARY_SKINNING_INCLUDED
#define URSA_SHADER_LIBRARY_SKINNING_INCLUDED

#include "Input.hlsl"

float4x4 UrsaGetSkinMatrix(uint4 joints, float4 weights)
{
	if (dot(weights, float4(1.0, 1.0, 1.0, 1.0)) <= 0.0)
		return float4x4(1.0, 0.0, 0.0, 0.0,
		                0.0, 1.0, 0.0, 0.0,
		                0.0, 0.0, 1.0, 0.0,
		                0.0, 0.0, 0.0, 1.0);
	return _UrsaBonePalette[joints.x] * weights.x + _UrsaBonePalette[joints.y] * weights.y +
	       _UrsaBonePalette[joints.z] * weights.z + _UrsaBonePalette[joints.w] * weights.w;
}

float3 UrsaSkinPosition(float3 positionOS, float4x4 skinMatrix)
{
	return mul(skinMatrix, float4(positionOS, 1.0)).xyz;
}

float3 UrsaSkinDirection(float3 directionOS, float4x4 skinMatrix)
{
	return mul((float3x3)skinMatrix, directionOS);
}

#endif
