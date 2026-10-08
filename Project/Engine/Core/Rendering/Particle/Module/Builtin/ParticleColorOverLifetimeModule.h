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
	//	ParticleColorOverLifetimeModule class
	//	寿命の進行度に応じて色をイージング補間する
	//============================================================================
	class ParticleColorOverLifetimeModule :
		public IParticleModule {
	public:

		// 保存と実行に使う設定
		struct Settings {

			// 発生時の色に掛ける色の始点と終点
			Color4 startColor = Color4::White();
			Color4 endColor = Color4(1.0f, 1.0f, 1.0f, 0.0f);
			// イージング
			EasingType easingType = EasingType::EaseOutSine;
			// 進行度のループ
			ParticleLoopSettings loop{};

			// カーブで色を制御するか
			bool useCurve = false;
			// 進行度から色を返すカーブ
			CurveColor4 curve{};
		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleColorOverLifetimeModule() = default;
		~ParticleColorOverLifetimeModule() override = default;

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
