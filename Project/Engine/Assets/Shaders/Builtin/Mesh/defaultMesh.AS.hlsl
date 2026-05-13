//============================================================================
//	include
//============================================================================

#include "defaultMesh.hlsli"

groupshared MeshDispatchPayload payload;

//============================================================================
//	main
//============================================================================
[numthreads(1, 1, 1)]
void main(uint3 groupID : SV_GroupID) {

	payload.meshletIndex = groupID.x;
	payload.instanceIndex = groupID.y;
	payload._pad0 = 0;
	payload._pad1 = 0;

	DispatchMesh(1, 1, 1, payload);
}
