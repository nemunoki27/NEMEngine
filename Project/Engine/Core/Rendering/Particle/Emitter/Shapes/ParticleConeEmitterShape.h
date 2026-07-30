#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleConeEmitterShape class
	//	底面円から開き角に沿って飛ばす発生形状
	//============================================================================
	class ParticleConeEmitterShape :
		public IParticleEmitterShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleConeEmitterShape() = default;
		~ParticleConeEmitterShape() override = default;

		void FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const override;
		void ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const override;

		void InitParticle(Vector3& position, Vector3& direction,
			const ParticleEmitterSettings& settings, bool is2D) const override;
		void DrawShape(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D) const override;
		bool DrawImGui(ParticleEmitterSettings& settings) const;
	};

} // Engine
