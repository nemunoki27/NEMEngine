#include "../Common/defaultMesh.hlsli"

void main(DepthVSOutput input) {

	ApplyMeshLODDither(input.position.xy, input.lodCoverage);
}
