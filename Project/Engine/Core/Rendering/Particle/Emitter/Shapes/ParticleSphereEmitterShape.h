#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleSphereEmitterShape class
	//	球面上から外向きに飛ばす発生形状
	//============================================================================
	class ParticleSphereEmitterShape :
		public IParticleEmitterShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSphereEmitterShape() = default;
		~ParticleSphereEmitterShape() override = default;

		void FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const override;
		void ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const override;

		void InitParticle(Vector3& position, Vector3& direction,
			const ParticleEmitterSettings& settings, bool is2D) const override;
		void DrawShape(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D) const override;
		bool DrawImGui(ParticleEmitterSettings& settings) const;
	};

} // Engine
