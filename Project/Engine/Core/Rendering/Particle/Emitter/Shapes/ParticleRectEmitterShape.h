#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleRectEmitterShape class
	//	有効な辺上の点から辺法線方向へ飛ばす発生形状、2D専用
	//============================================================================
	class ParticleRectEmitterShape :
		public IParticleEmitterShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleRectEmitterShape() = default;
		~ParticleRectEmitterShape() override = default;

		void FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const override;
		void ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const override;

		void InitParticle(Vector3& position, Vector3& direction,
			const ParticleEmitterSettings& settings, bool is2D) const override;
		void DrawShape(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D) const override;
		bool DrawImGui(ParticleEmitterSettings& settings) const;

		//--------- accessor -----------------------------------------------------

		bool Supports2D() const override { return true; }
		bool Supports3D() const override { return false; }
	};

} // Engine
