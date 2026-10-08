#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleLoopSettings.h>
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleScaleOverLifetimeModule class
	//	寿命の進行度に応じて軸別スケールをイージング補間する
	//============================================================================
	class ParticleScaleOverLifetimeModule :
		public IParticleModule {
	public:

		// 保存と実行に使う設定
		struct Settings {

			// 軸別スケールの始点と終点
			Vector3 startScale = Vector3::AnyInit(1.0f);
			Vector3 endScale = Vector3::AnyInit(0.0f);
			// イージング
			EasingType easingType = EasingType::EaseOutSine;
			// 進行度のループ
			ParticleLoopSettings loop{};

			// カーブでスケールを制御するか
			bool useCurve = false;
			// 進行度から軸別スケールを返すカーブ
			CurveVector3 curve{};
		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleScaleOverLifetimeModule() = default;
		~ParticleScaleOverLifetimeModule() override = default;

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
