#ifndef NEM_LIGHT_SHADER_DATA_HLSLI
#define NEM_LIGHT_SHADER_DATA_HLSLI

#ifndef NEM_LIGHT_COUNTS_REGISTER
#define NEM_LIGHT_COUNTS_REGISTER b0
#endif
#include "lightTypes.hlsli"

StructuredBuffer<DirectionalLight> gDirectionalLights : register(t6);
StructuredBuffer<PointLight> gPointLights : register(t7);
StructuredBuffer<SpotLight> gSpotLights : register(t8);
StructuredBuffer<RectLight> gRectLights : register(t12);

#endif
