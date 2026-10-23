#ifndef URSA_SHADER_LIBRARY_LIGHTING_INCLUDED
#define URSA_SHADER_LIBRARY_LIGHTING_INCLUDED

#include "BRDF.hlsl"
#include "InputData.hlsl"
#include "RealtimeLights.hlsl"
#include "Shadows.hlsl"

float3 UrsaEvaluateLighting(UrsaSurfaceData surface, UrsaInputData inputData)
{
	float3 normalWS = normalize(inputData.normalWS);
	float3 color = _UrsaAmbient.rgb * _UrsaAmbient.w *
	               (surface.albedo * (1.0 - surface.metallic) +
	                lerp(0.04.xxx, surface.albedo, surface.metallic) * (1.0 - surface.perceptualRoughness * 0.5)) *
	               surface.occlusion;
	[unroll]
	for (uint directionalIndex = 0; directionalIndex < 4; ++directionalIndex)
	{
		if (directionalIndex >= (uint)_UrsaCounts.x) break;
		UrsaLight light = UrsaGetDirectionalLight(directionalIndex);
		float shadow = directionalIndex == (uint)_UrsaMainLightIndex && _UrsaObjectData.y > 0.0
		                   ? UrsaCompareMainLightShadow(inputData.positionWS, normalWS, light.direction)
		                   : 1.0;
		color += UrsaDirectBRDF(surface, normalWS, inputData.viewDirectionWS, light.direction) * light.color * shadow;
	}
	[unroll]
	for (uint pointIndex = 0; pointIndex < 8; ++pointIndex)
	{
		if (pointIndex >= (uint)_UrsaCounts.y) break;
		UrsaLight light = UrsaGetPointLight(pointIndex, inputData.positionWS);
		color += UrsaDirectBRDF(surface, normalWS, inputData.viewDirectionWS, light.direction) * light.color *
		         light.attenuation;
	}
	return color + surface.emission;
}

#endif
