#ifndef URSA_SHADER_LIBRARY_COLOR_INCLUDED
#define URSA_SHADER_LIBRARY_COLOR_INCLUDED

float3 UrsaSRGBToLinear(float3 color)
{
	float3 low = color / 12.92;
	float3 high = pow(max((color + 0.055) / 1.055, 0.0), 2.4);
	return lerp(high, low, color <= 0.04045);
}

float3 UrsaLinearToSRGB(float3 color)
{
	float3 low = color * 12.92;
	float3 high = 1.055 * pow(max(color, 0.0), 1.0 / 2.4) - 0.055;
	return lerp(high, low, color <= 0.0031308);
}

float UrsaLuminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 UrsaReinhard(float3 color)
{
	return color / (1.0 + color);
}

#endif
