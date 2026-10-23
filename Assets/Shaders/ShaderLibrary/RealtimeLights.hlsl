#ifndef URSA_SHADER_LIBRARY_REALTIME_LIGHTS_INCLUDED
#define URSA_SHADER_LIBRARY_REALTIME_LIGHTS_INCLUDED

#include "Input.hlsl"

struct UrsaLight
{
	float3 direction;
	float3 color;
	float attenuation;
};

UrsaLight UrsaGetDirectionalLight(uint index)
{
	UrsaLight light;
	light.direction = normalize(-_UrsaDirectional[index].directionIntensity.xyz);
	light.color = _UrsaDirectional[index].color.rgb * _UrsaDirectional[index].directionIntensity.w;
	light.attenuation = 1.0;
	return light;
}

UrsaLight UrsaGetPointLight(uint index, float3 positionWS)
{
	UrsaLight light;
	float3 offset = _UrsaPoint[index].positionRange.xyz - positionWS;
	float distanceToLight = length(offset);
	float range = _UrsaPoint[index].positionRange.w;
	float rangeFade = saturate(1.0 - pow(distanceToLight / max(range, 1.0e-5), 4.0));
	light.direction = offset / max(distanceToLight, 1.0e-5);
	light.color = _UrsaPoint[index].colorIntensity.rgb;
	light.attenuation = rangeFade * rangeFade / (1.0 + distanceToLight * distanceToLight) *
	                    _UrsaPoint[index].colorIntensity.w;
	return light;
}

#endif
