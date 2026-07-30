#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Parametric/ParticleParametricShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleRingParametricShape class
	//	メッシュシェーダーで生成するRing形状
	//============================================================================
	class ParticleRingParametricShape :
		public IParticleParametricShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleRingParametricShape() = default;
		~ParticleRingParametricShape() override = default;

		//--------- accessor -----------------------------------------------------

		AssetID GetPipeline() const override;
		int32_t GetDivide(const ParticleRenderSettings& settings) const override;
	};

} // Engine
