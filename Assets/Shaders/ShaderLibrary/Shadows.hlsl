#ifndef URSA_SHADER_LIBRARY_SHADOWS_INCLUDED
#define URSA_SHADER_LIBRARY_SHADOWS_INCLUDED

#include "Input.hlsl"
#include "Math.hlsl"

TEXTURE2D(_UrsaDirectionalShadowAtlas);
SAMPLER(sampler_UrsaDirectionalShadowAtlas);

cbuffer UrsaShadowData
{
	float4x4 _UrsaDirectionalShadowMatrices[4];
	float4 _UrsaDirectionalShadowCascadeSplits;
	int _UrsaDirectionalShadowCascadeCount;
	int _UrsaDirectionalShadowSoft;
	int _UrsaMainLightIndex;
	float _UrsaDirectionalShadowStrength;
	float _UrsaDirectionalShadowBias;
	float _UrsaDirectionalShadowNormalBias;
	float _UrsaDirectionalShadowCascadeBlend;
	float4 _UrsaDirectionalShadowTexelSize;
	float4 _UrsaMainLightDirection;
}

float UrsaReadShadowDepth(float2 uv)
{
	return SAMPLE_TEXTURE2D(_UrsaDirectionalShadowAtlas, sampler_UrsaDirectionalShadowAtlas, uv).r;
}

float UrsaSampleMainLightShadowCascade(float3 positionWS, float3 normalWS, float3 lightDirectionWS, int cascadeIndex)
{
	float normalOffset =
	    (1.0 - saturate(dot(normalize(normalWS), normalize(lightDirectionWS)))) * _UrsaDirectionalShadowNormalBias;
	float3 biasedPosition = positionWS + normalize(normalWS) * normalOffset;
	float4 shadowPosition = mul(_UrsaDirectionalShadowMatrices[cascadeIndex], float4(biasedPosition, 1.0));
	if (shadowPosition.w <= 0.0)
	{
		return 1.0;
	}
	float3 shadowCoord = shadowPosition.xyz / shadowPosition.w;
	if (any(shadowCoord.xy < 0.0) || any(shadowCoord.xy > 1.0) || shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
	{
		return 1.0;
	}
	float2 tileScale = _UrsaDirectionalShadowCascadeCount == 1   ? float2(1.0, 1.0)
	                   : _UrsaDirectionalShadowCascadeCount == 2 ? float2(0.5, 1.0)
	                                                             : float2(0.5, 0.5);
	float2 tileIndex = _UrsaDirectionalShadowCascadeCount == 2   ? float2(cascadeIndex, 0.0)
	                   : _UrsaDirectionalShadowCascadeCount == 4 ? float2(cascadeIndex % 2, cascadeIndex / 2)
	                                                             : float2(0.0, 0.0);
	float2 tileMin = tileIndex * tileScale + _UrsaDirectionalShadowTexelSize.xy * 0.5;
	float2 tileMax = (tileIndex + 1.0) * tileScale - _UrsaDirectionalShadowTexelSize.xy * 0.5;
	float compareDepth = shadowCoord.z - _UrsaDirectionalShadowBias;
	float visibility = 0.0;
	if (_UrsaDirectionalShadowSoft == 0)
	{
		visibility = compareDepth <= UrsaReadShadowDepth(clamp(shadowCoord.xy, tileMin, tileMax)) ? 1.0 : 0.0;
	}
	else
	{
		[unroll] for (int y = -1; y <= 1; ++y)
		{
			[unroll] for (int x = -1; x <= 1; ++x)
			{
				float2 tap =
				    clamp(shadowCoord.xy + float2(x, y) * _UrsaDirectionalShadowTexelSize.xy, tileMin, tileMax);
				visibility += compareDepth <= UrsaReadShadowDepth(tap) ? (1.0 / 9.0) : 0.0;
			}
		}
	}
	return visibility;
}

float UrsaCompareMainLightShadow(float3 positionWS, float3 normalWS, float3 lightDirectionWS)
{
	if (_UrsaDirectionalShadowCascadeCount <= 0)
	{
		return 1.0;
	}
	float viewDepth = -mul(_UrsaView, float4(positionWS, 1.0)).z;
	int cascadeIndex = _UrsaDirectionalShadowCascadeCount - 1;
	[unroll] for (int index = 0; index < 4; ++index)
	{
		if (index >= _UrsaDirectionalShadowCascadeCount)
		{
			break;
		}
		if (viewDepth <= _UrsaDirectionalShadowCascadeSplits[index])
		{
			cascadeIndex = index;
			break;
		}
	}
	float lastCascadeFar = _UrsaDirectionalShadowCascadeSplits[_UrsaDirectionalShadowCascadeCount - 1];
	if (viewDepth > lastCascadeFar)
	{
		return 1.0;
	}
	float visibility = UrsaSampleMainLightShadowCascade(positionWS, normalWS, lightDirectionWS, cascadeIndex);
	float blendRatio = saturate(_UrsaDirectionalShadowCascadeBlend);
	[unroll] for (int boundary = 0; boundary < 3; ++boundary)
	{
		if (boundary + 1 >= _UrsaDirectionalShadowCascadeCount)
		{
			break;
		}
		float boundaryDepth = _UrsaDirectionalShadowCascadeSplits[boundary];
		float previousDepth = boundary == 0 ? 0.0 : _UrsaDirectionalShadowCascadeSplits[boundary - 1];
		float nextDepth = _UrsaDirectionalShadowCascadeSplits[boundary + 1];
		float blendWidth = min(boundaryDepth - previousDepth, nextDepth - boundaryDepth) * blendRatio;
		if (blendWidth > 0.0 && viewDepth >= boundaryDepth - blendWidth && viewDepth <= boundaryDepth + blendWidth)
		{
			float blend = smoothstep(boundaryDepth - blendWidth, boundaryDepth + blendWidth, viewDepth);
			float nextVisibility =
			    UrsaSampleMainLightShadowCascade(positionWS, normalWS, lightDirectionWS, boundary + 1);
			visibility = lerp(UrsaSampleMainLightShadowCascade(positionWS, normalWS, lightDirectionWS, boundary),
			                  nextVisibility, blend);
			break;
		}
	}
	float lastCascadeNear = _UrsaDirectionalShadowCascadeCount > 1
	                            ? _UrsaDirectionalShadowCascadeSplits[_UrsaDirectionalShadowCascadeCount - 2]
	                            : 0.0;
	float fadeWidth = (lastCascadeFar - lastCascadeNear) * blendRatio;
	if (fadeWidth > 0.0)
	{
		float fade = 1.0 - smoothstep(lastCascadeFar - fadeWidth, lastCascadeFar, viewDepth);
		visibility = lerp(1.0, visibility, fade);
	}
	return lerp(1.0, visibility, saturate(_UrsaDirectionalShadowStrength));
}

#endif
