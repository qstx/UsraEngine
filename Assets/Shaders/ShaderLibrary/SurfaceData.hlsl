#ifndef URSA_SHADER_LIBRARY_SURFACE_DATA_INCLUDED
#define URSA_SHADER_LIBRARY_SURFACE_DATA_INCLUDED

struct UrsaSurfaceData
{
	float3 albedo;
	float metallic;
	float perceptualRoughness;
	float3 normalTS;
	float occlusion;
	float3 emission;
	float alpha;
};

UrsaSurfaceData UrsaInitializeSurfaceData(float3 albedo, float alpha)
{
	UrsaSurfaceData surface;
	surface.albedo = albedo;
	surface.metallic = 0.0;
	surface.perceptualRoughness = 1.0;
	surface.normalTS = float3(0.0, 0.0, 1.0);
	surface.occlusion = 1.0;
	surface.emission = 0.0;
	surface.alpha = alpha;
	return surface;
}

#endif
