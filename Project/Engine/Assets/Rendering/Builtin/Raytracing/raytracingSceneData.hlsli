#ifndef NEM_RAYTRACING_SCENE_DATA
#define NEM_RAYTRACING_SCENE_DATA

struct RaytracingInstanceShaderData {

	uint vertexDescriptorIndex;
	uint indexDescriptorIndex;
	uint vertexOffset;
	uint geometryDataOffset;

	uint renderFlags;
	uint entityIndex;
	uint entityGeneration;
	uint _pad;
};
struct RaytracingGeometryShaderData {

	uint subMeshDataIndex;
	uint indexOffset;
	uint pickRecordIndex;
	uint parameterOffset;
	float4 boundsSphere;
};

#endif
