#ifndef URSA_SHADER_LIBRARY_SURFACE_INPUT_INCLUDED
#define URSA_SHADER_LIBRARY_SURFACE_INPUT_INCLUDED

#include "Core.hlsl"
#include "SurfaceData.hlsl"

cbuffer UrsaPerMaterial
{
	float4 _BaseColor;
	float _Metallic;
	float _Roughness;
	float _NormalScale;
	float _OcclusionStrength;
	float4 _Emissive;
	int _AlphaMode;
	float _AlphaCutoff;
}

TEXTURE2D(_BaseMap);
SAMPLER(sampler_BaseMap);
TEXTURE2D(_MetallicRoughnessMap);
SAMPLER(sampler_MetallicRoughnessMap);
TEXTURE2D(_NormalMap);
SAMPLER(sampler_NormalMap);
TEXTURE2D(_OcclusionMap);
SAMPLER(sampler_OcclusionMap);
TEXTURE2D(_EmissiveMap);
SAMPLER(sampler_EmissiveMap);

UrsaSurfaceData UrsaSampleSurfaceData(float2 uv)
{
	float4 baseSample = SAMPLE_TEXTURE2D(_BaseMap, sampler_BaseMap, uv);
	float4 metallicRoughness = SAMPLE_TEXTURE2D(_MetallicRoughnessMap, sampler_MetallicRoughnessMap, uv);
	float3 normalSample = SAMPLE_TEXTURE2D(_NormalMap, sampler_NormalMap, uv).xyz * 2.0 - 1.0;
	float occlusionSample = SAMPLE_TEXTURE2D(_OcclusionMap, sampler_OcclusionMap, uv).r;
	float3 emissionSample = SAMPLE_TEXTURE2D(_EmissiveMap, sampler_EmissiveMap, uv).rgb;
	UrsaSurfaceData surface = UrsaInitializeSurfaceData(_BaseColor.rgb * baseSample.rgb, _BaseColor.a * baseSample.a);
	surface.metallic = saturate(_Metallic * metallicRoughness.b);
	surface.perceptualRoughness = clamp(_Roughness * metallicRoughness.g, 0.04, 1.0);
	surface.normalTS = normalize(float3(normalSample.xy * _NormalScale, normalSample.z));
	surface.occlusion = lerp(1.0, occlusionSample, saturate(_OcclusionStrength));
	surface.emission = _Emissive.rgb * emissionSample;
	return surface;
}

float UrsaResolveAlpha(UrsaSurfaceData surface)
{
	if (_AlphaMode == 1 && surface.alpha < _AlphaCutoff)
		discard;
	return _AlphaMode == 2 ? surface.alpha : 1.0;
}

#endif
