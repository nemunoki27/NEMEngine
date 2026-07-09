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

		void PackShapeParams(const nlohmann::json& params, Vector4& start, Vector4& end) const override;
		bool DrawImGui(nlohmann::json& params) const override;

		//--------- accessor -----------------------------------------------------

		AssetID GetPipeline() const override;
		int32_t GetDivide(const ParticleRenderSettings& settings) const override;
	};

	ENGINE_REGISTER_PARTICLE_PARAMETRIC_SHAPE(ParticleRingParametricShape, PrimitiveType::Ring);
} // Engine
