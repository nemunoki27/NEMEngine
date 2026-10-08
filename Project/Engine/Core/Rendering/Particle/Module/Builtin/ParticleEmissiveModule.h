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

		// 保存と実行に使う設定
		struct Settings {

			// 発光色の始点と終点
			Color3 startColor = Color3::White();
			Color3 endColor = Color3::White();
			// 発光の強さの始点と終点
			float startIntensity = 1.0f;
			float endIntensity = 1.0f;
			// 発光色と強度のイージング
			EasingType colorEasingType = EasingType::EaseOutSine;
			EasingType intensityEasingType = EasingType::EaseOutSine;
		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleEmissiveModule() = default;
		~ParticleEmissiveModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;

		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnUpdate(Particle& particle, float deltaTime) override;

		//--------- accessor -----------------------------------------------------

		const Settings& GetSettings() const { return settings_; }
		void SetSettings(const Settings& settings) { settings_ = settings; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Settings settings_{};

	};

} // Engine
