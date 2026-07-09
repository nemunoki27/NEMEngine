#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Parametric/ParticleParametricShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleCylinderParametricShape class
	//	メッシュシェーダーで生成するCylinder形状
	//============================================================================
	class ParticleCylinderParametricShape :
		public IParticleParametricShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleCylinderParametricShape() = default;
		~ParticleCylinderParametricShape() override = default;

		void PackShapeParams(const nlohmann::json& params, Vector4& start, Vector4& end) const override;
		bool DrawImGui(nlohmann::json& params) const override;

		//--------- accessor -----------------------------------------------------

		AssetID GetPipeline() const override;
		int32_t GetDivide(const ParticleRenderSettings& settings) const override;
	};

	ENGINE_REGISTER_PARTICLE_PARAMETRIC_SHAPE(ParticleCylinderParametricShape, PrimitiveType::Cylinder);
} // Engine
