#ifndef NEM_LIGHTING_COMMON_HLSLI
#define NEM_LIGHTING_COMMON_HLSLI

//============================================================================
//	VSOutputに依存しないライティング共通定義、MeshとPrimitiveで共有する
//============================================================================

//============================================================================
//	include
//============================================================================
#include "../../Common/mathConstants.hlsli"

//============================================================================
//	定数
//============================================================================
static const uint kNoTexture = 0xFFFFFFFF;

//============================================================================
//	ライト定義
//============================================================================
#ifndef NEM_LIGHT_COUNTS_REGISTER
#define NEM_LIGHT_COUNTS_REGISTER b2
#endif
#ifndef NEM_DIRECTIONAL_LIGHTS_REGISTER
#define NEM_DIRECTIONAL_LIGHTS_REGISTER t4
#endif
#ifndef NEM_POINT_LIGHTS_REGISTER
#define NEM_POINT_LIGHTS_REGISTER t5
#endif
#ifndef NEM_SPOT_LIGHTS_REGISTER
#define NEM_SPOT_LIGHTS_REGISTER t6
#endif
#ifndef NEM_RECT_LIGHTS_REGISTER
#define NEM_RECT_LIGHTS_REGISTER t7
#endif
#ifndef NEM_LIGHTING_SAMPLER_REGISTER
#define NEM_LIGHTING_SAMPLER_REGISTER s0
#endif

#include "../../Lighting/lightTypes.hlsli"

StructuredBuffer<DirectionalLight> gDirectionalLights :
	register(NEM_DIRECTIONAL_LIGHTS_REGISTER);
StructuredBuffer<PointLight> gPointLights :
	register(NEM_POINT_LIGHTS_REGISTER);
StructuredBuffer<SpotLight> gSpotLights :
	register(NEM_SPOT_LIGHTS_REGISTER);
StructuredBuffer<RectLight> gRectLights :
	register(NEM_RECT_LIGHTS_REGISTER);

SamplerState gSampler : register(NEM_LIGHTING_SAMPLER_REGISTER);

//============================================================================
//	距離減衰
//============================================================================
#include "../../Lighting/lightAttenuation.hlsli"

#endif // NEM_LIGHTING_COMMON_HLSLI
