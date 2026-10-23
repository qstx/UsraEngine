#ifndef URSA_SHADER_LIBRARY_BRDF_INCLUDED
#define URSA_SHADER_LIBRARY_BRDF_INCLUDED

#include "Math.hlsl"
#include "SurfaceData.hlsl"

float3 UrsaFresnelSchlick(float cosine, float3 reflectance0)
{
	return reflectance0 + (1.0 - reflectance0) * pow(saturate(1.0 - cosine), 5.0);
}

float3 UrsaDirectBRDF(UrsaSurfaceData surface, float3 normalWS, float3 viewDirectionWS, float3 lightDirectionWS)
{
	float nDotL = saturate(dot(normalWS, lightDirectionWS));
	float nDotV = max(dot(normalWS, viewDirectionWS), 1.0e-4);
	if (nDotL <= 0.0)
		return 0.0;
	float3 halfDirection = UrsaSafeNormalize(viewDirectionWS + lightDirectionWS, normalWS);
	float nDotH = saturate(dot(normalWS, halfDirection));
	float vDotH = saturate(dot(viewDirectionWS, halfDirection));
	float roughness = saturate(surface.perceptualRoughness);
	float alpha = max(roughness * roughness, 0.002);
	float alphaSquared = alpha * alpha;
	float distributionDenominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
	float distribution = alphaSquared / max(URSA_PI * distributionDenominator * distributionDenominator, 1.0e-6);
	float k = UrsaSquare(roughness + 1.0) * 0.125;
	float visibilityV = nDotV / max(nDotV * (1.0 - k) + k, 1.0e-5);
	float visibilityL = nDotL / max(nDotL * (1.0 - k) + k, 1.0e-5);
	float3 reflectance0 = lerp(0.04.xxx, surface.albedo, saturate(surface.metallic));
	float3 fresnel = UrsaFresnelSchlick(vDotH, reflectance0);
	float3 specular = distribution * visibilityV * visibilityL * fresnel /
	                   max(4.0 * nDotV * nDotL, 1.0e-5);
	float3 diffuse = (1.0 - fresnel) * (1.0 - saturate(surface.metallic)) * surface.albedo * URSA_INV_PI;
	return (diffuse + specular) * nDotL;
}

#endif
