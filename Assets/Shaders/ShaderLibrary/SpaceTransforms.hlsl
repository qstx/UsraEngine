#ifndef URSA_SHADER_LIBRARY_SPACE_TRANSFORMS_INCLUDED
#define URSA_SHADER_LIBRARY_SPACE_TRANSFORMS_INCLUDED

#include "Input.hlsl"
#include "Math.hlsl"

float3 UrsaTransformObjectToWorld(float3 positionOS)
{
	return mul(_UrsaModel, float4(positionOS, 1.0)).xyz;
}

float3 UrsaTransformObjectToWorldDir(float3 directionOS)
{
	return mul((float3x3)_UrsaModel, directionOS);
}

float3 UrsaTransformObjectToWorldNormal(float3 normalOS)
{
	return UrsaSafeNormalize(mul((float3x3)_UrsaNormal, normalOS), float3(0.0, 1.0, 0.0));
}

float4 UrsaTransformWorldToHClip(float3 positionWS)
{
	return mul(_UrsaViewProjection, float4(positionWS, 1.0));
}

float4 UrsaTransformObjectToHClip(float3 positionOS)
{
	return UrsaTransformWorldToHClip(UrsaTransformObjectToWorld(positionOS));
}

float UrsaGetOddNegativeScale()
{
	return determinant((float3x3)_UrsaModel) < 0.0 ? -1.0 : 1.0;
}

#endif
