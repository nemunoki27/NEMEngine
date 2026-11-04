//============================================================================
//	include
//============================================================================
#include "../Common/defaultMeshOutline.hlsli"

groupshared MeshDispatchPayload payload;

//============================================================================
//	main
//============================================================================
[numthreads(32, 1, 1)]
void main(uint groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID) {

	const uint localMeshletIndex = groupID.x * 32u + groupThreadID;
	const uint instanceIndex = groupID.y;
	const uint meshletIndex = meshletOffset + localMeshletIndex;
	const bool inRange = instanceIndex < instanceCount && localMeshletIndex < meshletCount;
	// IsMeshletVisibleは共通hlsli側のアウトライン対応カリングを使う
	bool visible = inRange && IsMeshletVisible(meshletIndex, instanceIndex);

	const uint visibleOffset = WavePrefixCountBits(visible);
	const uint visibleCount = WaveActiveCountBits(visible);
	if (visible) {

		payload.meshletIndices[visibleOffset] = meshletIndex;
		payload.instanceIndices[visibleOffset] = instanceIndex;
	}
	GroupMemoryBarrierWithGroupSync();

	DispatchMesh(visibleCount, 1, 1, payload);
}
