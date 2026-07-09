#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleCircleEmitterShape class
	//	円弧上から外向きに飛ばす発生形状、3DはXZ平面で2DはXY平面
	//============================================================================
	class ParticleCircleEmitterShape :
		public IParticleEmitterShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleCircleEmitterShape() = default;
		~ParticleCircleEmitterShape() override = default;

		void FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const override;
		void ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const override;

		void InitParticle(Vector3& position, Vector3& direction,
			const ParticleEmitterSettings& settings, bool is2D) const override;
		void DrawShape(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D) const override;
		bool DrawImGui(ParticleEmitterSettings& settings) const override;

		//--------- accessor -----------------------------------------------------

		bool Supports2D() const override { return true; }
	};

	ENGINE_REGISTER_PARTICLE_EMITTER_SHAPE(ParticleCircleEmitterShape, ParticleEmitterShape::Circle);
} // Engine
