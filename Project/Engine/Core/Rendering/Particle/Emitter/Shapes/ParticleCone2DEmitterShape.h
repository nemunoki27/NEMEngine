#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleCone2DEmitterShape class
	//	底辺の線分から開き角の範囲で上向きに飛ばす発生形状、2D専用
	//============================================================================
	class ParticleCone2DEmitterShape :
		public IParticleEmitterShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleCone2DEmitterShape() = default;
		~ParticleCone2DEmitterShape() override = default;

		void FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const override;
		void ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const override;

		void InitParticle(Vector3& position, Vector3& direction,
			const ParticleEmitterSettings& settings, bool is2D) const override;
		void DrawShape(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D) const override;

		//--------- accessor -----------------------------------------------------

		bool Supports2D() const override { return true; }
		bool Supports3D() const override { return false; }
	};

} // Engine
