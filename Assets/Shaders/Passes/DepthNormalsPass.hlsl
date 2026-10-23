#ifndef URSA_DEPTH_NORMALS_PASS_INCLUDED
#define URSA_DEPTH_NORMALS_PASS_INCLUDED

#include "../ShaderLibrary/Core.hlsl"

struct UrsaDepthNormalsAttributes
{
	float3 positionOS : POSITION;
	float3 normalOS : NORMAL;
	float2 uv : TEXCOORD0;
	uint4 joints : BLENDINDICES;
	float4 weights : BLENDWEIGHT;
};

struct UrsaDepthNormalsVaryings
{
	float4 positionCS : SV_Position;
	float2 uv : TEXCOORD0;
	float3 normalWS : TEXCOORD1;
};

UrsaDepthNormalsVaryings UrsaDepthNormalsVertex(UrsaDepthNormalsAttributes input)
{
	UrsaDepthNormalsVaryings output;
	float4x4 skin = UrsaGetSkinMatrix(input.joints, input.weights);
	output.positionCS = UrsaTransformObjectToHClip(UrsaSkinPosition(input.positionOS, skin));
	output.normalWS = UrsaTransformObjectToWorldNormal(UrsaSkinDirection(input.normalOS, skin));
	output.uv = input.uv;
	return output;
}

float4 UrsaDepthNormalsFragment(UrsaDepthNormalsVaryings input) : SV_Target0
{
	URSA_DEPTH_ALPHA_CLIP(input.uv);
	return float4(normalize(input.normalWS), 1.0);
}

#endif
