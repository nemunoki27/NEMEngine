#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleTorusEmitterShape class
	//	主円周上の管内から管の外向きに飛ばす発生形状
	//============================================================================
	class ParticleTorusEmitterShape :
		public IParticleEmitterShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleTorusEmitterShape() = default;
		~ParticleTorusEmitterShape() override = default;

		void FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const override;
		void ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const override;

		void InitParticle(Vector3& position, Vector3& direction,
			const ParticleEmitterSettings& settings, bool is2D) const override;
	};

} // Engine
