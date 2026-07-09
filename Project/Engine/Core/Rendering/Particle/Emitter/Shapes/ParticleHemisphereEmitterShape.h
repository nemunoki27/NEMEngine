#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleHemisphereEmitterShape class
	//	Y上向きの半球面から外向きに飛ばす発生形状
	//============================================================================
	class ParticleHemisphereEmitterShape :
		public IParticleEmitterShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleHemisphereEmitterShape() = default;
		~ParticleHemisphereEmitterShape() override = default;

		void FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const override;
		void ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const override;

		void InitParticle(Vector3& position, Vector3& direction,
			const ParticleEmitterSettings& settings, bool is2D) const override;
		void DrawShape(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D) const override;
		bool DrawImGui(ParticleEmitterSettings& settings) const override;
	};

	ENGINE_REGISTER_PARTICLE_EMITTER_SHAPE(ParticleHemisphereEmitterShape, ParticleEmitterShape::Hemisphere);
} // Engine
