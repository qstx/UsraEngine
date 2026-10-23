#ifndef URSA_SHADOW_CASTER_PASS_INCLUDED
#define URSA_SHADOW_CASTER_PASS_INCLUDED

#include "../ShaderLibrary/Core.hlsl"
#include "../ShaderLibrary/Shadows.hlsl"

struct UrsaShadowCasterAttributes
{
	float3 positionOS : POSITION;
	float3 normalOS : NORMAL;
	float2 uv : TEXCOORD0;
	uint4 joints : BLENDINDICES;
	float4 weights : BLENDWEIGHT;
};

struct UrsaShadowCasterVaryings
{
	float4 positionCS : SV_Position;
	float2 uv : TEXCOORD0;
};

UrsaShadowCasterVaryings UrsaShadowCasterVertex(UrsaShadowCasterAttributes input)
{
	UrsaShadowCasterVaryings output;
	float4x4 skin = UrsaGetSkinMatrix(input.joints, input.weights);
	float3 positionOS = UrsaSkinPosition(input.positionOS, skin);
	float3 normalOS = UrsaSkinDirection(input.normalOS, skin);
	float3 positionWS = UrsaTransformObjectToWorld(positionOS);
	float3 normalWS = UrsaTransformObjectToWorldNormal(normalOS);
	float3 lightDirectionWS = UrsaSafeNormalize(-_UrsaMainLightDirection.xyz, float3(0.0, 1.0, 0.0));
	float normalBias = (1.0 - saturate(dot(normalWS, lightDirectionWS))) * _UrsaDirectionalShadowNormalBias;
	positionWS += normalWS * normalBias;
	output.positionCS = UrsaTransformWorldToHClip(positionWS);
	output.uv = input.uv;
	return output;
}

void UrsaShadowCasterFragment(UrsaShadowCasterVaryings input)
{
	URSA_SHADOW_ALPHA_CLIP(input.uv);
}

#endif
