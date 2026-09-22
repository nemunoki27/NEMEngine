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
	//	ParticleSizeOverLifetimeModule class
	//	寿命の進行度に応じて大きさをイージング補間する
	//============================================================================
	class ParticleSizeOverLifetimeModule :
		public IParticleModule {
	public:

		// 保存と実行に使う設定
		struct Settings {

			// 発生時の大きさに掛ける倍率の始点と終点
			float startScale = 1.0f;
			float endScale = 0.0f;
			// イージング
			EasingType easingType = EasingType::EaseOutSine;
			// 進行度のループ
			ParticleLoopSettings loop{};

			// カーブで倍率を制御するか
			bool useCurve = false;
			// 進行度から倍率を返すカーブ
			CurveFloat curve{};
		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSizeOverLifetimeModule() = default;
		~ParticleSizeOverLifetimeModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;

		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnUpdate(Particle& particle, float deltaTime) override;
		//--------- accessor -----------------------------------------------------

		const Settings& GetSettings() const { return settings_; }
		void SetSettings(const Settings& settings) { settings_ = settings; }
	protected:
		// 派生モジュールの既定範囲を設定する
		void SetScaleRange(float start, float end) { settings_.startScale = start; settings_.endScale = end; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Settings settings_{};

	};

} // Engine
