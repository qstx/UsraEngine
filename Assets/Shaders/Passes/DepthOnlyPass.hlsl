#ifndef URSA_DEPTH_ONLY_PASS_INCLUDED
#define URSA_DEPTH_ONLY_PASS_INCLUDED

#include "../ShaderLibrary/Core.hlsl"

struct UrsaDepthOnlyAttributes
{
	float3 positionOS : POSITION;
	float2 uv : TEXCOORD0;
	uint4 joints : BLENDINDICES;
	float4 weights : BLENDWEIGHT;
};

struct UrsaDepthOnlyVaryings
{
	float4 positionCS : SV_Position;
	float2 uv : TEXCOORD0;
};

UrsaDepthOnlyVaryings UrsaDepthOnlyVertex(UrsaDepthOnlyAttributes input)
{
	UrsaDepthOnlyVaryings output;
	float4x4 skin = UrsaGetSkinMatrix(input.joints, input.weights);
	output.positionCS = UrsaTransformObjectToHClip(UrsaSkinPosition(input.positionOS, skin));
	output.uv = input.uv;
	return output;
}

void UrsaDepthOnlyFragment(UrsaDepthOnlyVaryings input)
{
	URSA_DEPTH_ALPHA_CLIP(input.uv);
}

#endif
