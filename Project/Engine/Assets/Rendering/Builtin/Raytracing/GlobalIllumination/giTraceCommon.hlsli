#include "../../Mesh/Common/deferredGBuffer.hlsli"
#include "../../PostProcess/Common/postProcessFrame.hlsli"
#include "giGrid.hlsli"
#include "../raytracingSceneData.hlsli"
#include "../../Common/hexTiling.hlsli"
#include "../../Primitive/primitiveUV.hlsli"
#include "../../Primitive/primitiveInstanceColor.hlsli"
#include "../../Lighting/environmentSampling.hlsli"
#include "../../Lighting/lightShadowFilter.hlsli"
#define NEM_DIRECTIONAL_LIGHTS_REGISTER t16
#define NEM_POINT_LIGHTS_REGISTER t17
#define NEM_SPOT_LIGHTS_REGISTER t18
#define NEM_RECT_LIGHTS_REGISTER t19
#include "../../Mesh/Common/lightingCommon.hlsli"
#include "../../Common/pbrMath.hlsli"

cbuffer RaytracingViewConstants : register(b3) {

	float4x4 gView;
	float4x4 gProjection;
	float4x4 gInverseView;
	float4x4 gInverseProjection;
	float4x4 gInverseViewProjection;

	float3 gCameraPosition;
	float gMaxReflectionDistance;

	float2 gRenderSize;
	float2 gInvRenderSize;

	float gShadowNormalBias;
	float gReflectionIntensity;
	float gNearClip;
	float gFarClip;

	float gReflectionNormalBias;
	float gReflectionViewBias;
	float gReflectionMinHitDistance;
	float gReflectionMaxRoughness;

	float gReflectionRoughnessFade;
	float gSkyIntensity;
	float gFresnelMin;
	float gReflectionPadding;

	float4 gSkyboxColor;
	uint gSkyboxCubemapIndex;
	uint gHasSkybox;
	float gIBLIntensity;
	uint gFrameIndex;
	uint gEnvironmentSpecularIndex;
	uint gEnvironmentBRDFIndex;
	uint gPhysicalEnvironmentEnabled;
	uint gEnvironmentIrradianceIndex;
};


cbuffer PostProcessParameters : register(b1) {
	uint rayCount;
	uint cullingMask;
	uint updatePeriod;
	float ambientIntensity;
};

RaytracingAccelerationStructure gSceneTLAS : register(t0);
StructuredBuffer<RaytracingInstanceShaderData> gRaytracingSceneInstances : register(t1);
StructuredBuffer<RaytracingGeometryShaderData> gRaytracingGeometries : register(t2);
StructuredBuffer<SubMeshShaderData> gRaytracingSubMeshes : register(t3);
ByteAddressBuffer gRaytracingMaterialParameters : register(t0, space34);

cbuffer GIChangeBounds : register(b4) {
	float4 changeBounds[8];
	uint lightingChanged;
	uint emitterCount;
	uint cacheFrame;
	uint cacheConverged;
	uint hasCoarseBricks;
	uint showCacheUpdates;
	uint updateSpeed;
	uint layoutChanged;
};

#include "giEmitters.hlsli"

