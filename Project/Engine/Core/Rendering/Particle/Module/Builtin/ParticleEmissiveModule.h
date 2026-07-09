#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleEmissiveModule class
	//	寿命の進行度に応じて発光色と強さをイージング補間する
	//============================================================================
	class ParticleEmissiveModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleEmissiveModule() = default;
		~ParticleEmissiveModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 発光色の始点と終点
		Color3 startColor_ = Color3::White();
		Color3 endColor_ = Color3::White();
		// 発光の強さの始点と終点
		float startIntensity_ = 1.0f;
		float endIntensity_ = 1.0f;
		// イージング
		EasingType easingType_ = EasingType::EaseOutSine;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleEmissiveModule, "Emissive");
} // Engine
