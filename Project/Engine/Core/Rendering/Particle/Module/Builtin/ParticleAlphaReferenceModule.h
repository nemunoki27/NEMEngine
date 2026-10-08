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

		// 保存と実行に使う設定
		struct Settings {

			// 閾値の始点と終点
			float startReference = 0.0f;
			float endReference = 0.5f;
			// イージング
			EasingType easingType = EasingType::EaseOutSine;
		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleAlphaReferenceModule() = default;
		~ParticleAlphaReferenceModule() override = default;

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
