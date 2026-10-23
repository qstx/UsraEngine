#ifndef URSA_SHADER_LIBRARY_INPUT_DATA_INCLUDED
#define URSA_SHADER_LIBRARY_INPUT_DATA_INCLUDED

struct UrsaInputData
{
	float3 positionWS;
	float3 normalWS;
	float3 viewDirectionWS;
	float4 shadowCoord;
};

UrsaInputData UrsaInitializeInputData(float3 positionWS, float3 normalWS, float3 cameraPositionWS)
{
	UrsaInputData inputData;
	inputData.positionWS = positionWS;
	inputData.normalWS = normalize(normalWS);
	inputData.viewDirectionWS = normalize(cameraPositionWS - positionWS);
	inputData.shadowCoord = 0.0;
	return inputData;
}

#endif
