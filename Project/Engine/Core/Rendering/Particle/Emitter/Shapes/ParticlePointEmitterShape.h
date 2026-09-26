#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticlePointEmitterShape class
	//	原点から指定方向へ飛ばす発生形状
	//============================================================================
	class ParticlePointEmitterShape :
		public IParticleEmitterShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticlePointEmitterShape() = default;
		~ParticlePointEmitterShape() override = default;

		void FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const override;
		void ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const override;

		void InitParticle(Vector3& position, Vector3& direction,
			const ParticleEmitterSettings& settings, bool is2D) const override;

		//--------- accessor -----------------------------------------------------

		bool Supports2D() const override { return true; }
	};

} // Engine
