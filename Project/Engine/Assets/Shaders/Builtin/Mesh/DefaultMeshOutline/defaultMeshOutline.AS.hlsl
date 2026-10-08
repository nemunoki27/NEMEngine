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
	const uint instanceIndex = groupID.y % instanceCount;
	const uint lodPart = groupID.y / instanceCount;
	uint meshletIndex = 0u;
	float lodCoverage = 1.0f;
	bool inRange = instanceIndex < instanceCount && lodPart < 2u;
	if (inRange) {

		MeshLODSelection selection = ResolveMeshLODSelection(
			gMeshInstances[instanceIndex]);
		const uint lodIndex = lodPart == 0u ?
			selection.firstLOD : selection.secondLOD;
		lodCoverage = lodPart == 0u ?
			selection.firstCoverage : selection.secondCoverage;
		inRange = lodIndex != 0xFFFFFFFFu && lodCoverage > 0.0f;
		if (inRange) {
			inRange = localMeshletIndex < lodMeshletCounts[lodIndex];
			meshletIndex = lodMeshletOffsets[lodIndex] + localMeshletIndex;
		}
	}
	// IsMeshletVisibleは共通hlsli側のアウトライン対応カリングを使う
	bool visible = inRange && IsMeshletVisible(meshletIndex, instanceIndex);

	const uint visibleOffset = WavePrefixCountBits(visible);
	const uint visibleCount = WaveActiveCountBits(visible);
	if (visible) {

		payload.meshletIndices[visibleOffset] = meshletIndex;
		payload.instanceIndices[visibleOffset] = instanceIndex;
		payload.lodCoverages[visibleOffset] = lodCoverage;
	}
	GroupMemoryBarrierWithGroupSync();

	DispatchMesh(visibleCount, 1, 1, payload);
}
