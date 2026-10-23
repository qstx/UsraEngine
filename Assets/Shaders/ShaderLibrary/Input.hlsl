#ifndef URSA_SHADER_LIBRARY_INPUT_INCLUDED
#define URSA_SHADER_LIBRARY_INPUT_INCLUDED

struct UrsaDirectionalLight
{
	float4 directionIntensity;
	float4 color;
};

struct UrsaPointLight
{
	float4 positionRange;
	float4 colorIntensity;
};

cbuffer UrsaPerObject
{
	float4x4 _UrsaViewProjection;
	float4x4 _UrsaView;
	float4x4 _UrsaModel;
	float4x4 _UrsaNormal;
	float4 _UrsaObjectData;
}

cbuffer UrsaScene
{
	float4 _UrsaCameraPosition;
	float4 _UrsaAmbient;
	int4 _UrsaCounts;
	UrsaDirectionalLight _UrsaDirectional[4];
	UrsaPointLight _UrsaPoint[8];
}

StructuredBuffer<float4x4> _UrsaBonePalette;

#endif
