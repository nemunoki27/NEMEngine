#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleAlphaReferenceModule class
	//	アルファ棄却の閾値を寿命の進行度でイージング補間する、閾値未満のピクセルは描かれない
	//============================================================================
	class ParticleAlphaReferenceModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleAlphaReferenceModule() = default;
		~ParticleAlphaReferenceModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 閾値の始点と終点
		float startReference_ = 0.0f;
		float endReference_ = 0.5f;
		// イージング
		EasingType easingType_ = EasingType::EaseOutSine;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleAlphaReferenceModule, "AlphaReference");
} // Engine
