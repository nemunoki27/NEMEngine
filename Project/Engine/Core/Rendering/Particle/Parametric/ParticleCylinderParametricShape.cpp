#include "ParticleCylinderParametricShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

//============================================================================
//	ParticleCylinderParametricShape classMethods
//============================================================================
Engine::AssetID Engine::ParticleCylinderParametricShape::GetPipeline() const {

	return BuiltinAssets::Pipelines::ParticleCylinderMS;
}

int32_t Engine::ParticleCylinderParametricShape::GetDivide(const ParticleRenderSettings& settings) const {

	return settings.cylinder.radialDivide;
}
