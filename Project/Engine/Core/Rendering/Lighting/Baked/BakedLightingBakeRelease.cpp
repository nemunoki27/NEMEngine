#include "BakedLightingBakeState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

//============================================================================
//	BakedLighting releaseMethods
//============================================================================
void Engine::ReleaseBakedLightingGPU(BakedLightingBakeState& state) {

	// 所有を解除しても提出済みのFrameまでGPU資源を残す
	state.tlas.Release();
	state.blas.Release();
	state.triangles.Release();
	state.positions.Release();
	state.indices.Release();
	state.texels.Release();
	state.materials.Release();
	state.probes.Release();
	state.captures.Release();
	state.directionalLights.Release();
	state.pointLights.Release();
	state.spotLights.Release();
	state.rectLights.Release();
	state.output.Release();
	state.probeOutput.Release();
	state.constants.Release();
	state.lightCounts.Release();
	state.pipeline.reset();
	state.materialResolver.Clear();
	state.skyOwnership = {};
	if (state.readback) state.retirement->Retire(std::move(state.readback));
	if (state.probeReadback) state.retirement->Retire(std::move(state.probeReadback));
}
