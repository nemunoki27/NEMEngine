#include "ParticleRingParametricShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

//============================================================================
//	ParticleRingParametricShape classMethods
//============================================================================
Engine::AssetID Engine::ParticleRingParametricShape::GetPipeline() const {

	return BuiltinAssets::Pipelines::ParticleRingMS;
}

int32_t Engine::ParticleRingParametricShape::GetDivide(const ParticleRenderSettings& settings) const {

	return settings.ring.divide;
}
