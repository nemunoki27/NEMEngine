#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleLoopSettings.h>
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

namespace Engine {

	//============================================================================
	//	ParticleSizeOverLifetimeModule class
	//	寿命の進行度に応じて大きさをイージング補間する
	//============================================================================
	class ParticleSizeOverLifetimeModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSizeOverLifetimeModule() = default;
		~ParticleSizeOverLifetimeModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnUpdate(Particle& particle, float deltaTime) override;
	protected:
		// 派生モジュールの既定範囲を設定する
		void SetScaleRange(float start, float end) { startScale_ = start; endScale_ = end; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 発生時の大きさに掛ける倍率の始点と終点
		float startScale_ = 1.0f;
		float endScale_ = 0.0f;
		// イージング
		EasingType easingType_ = EasingType::EaseOutSine;
		// 進行度のループ
		ParticleLoopSettings loop_{};

		// カーブで倍率を制御するか
		bool useCurve_ = false;
		// 進行度から倍率を返すカーブ
		CurveFloat curve_{};
		// カーブ編集の状態、編集UIでのみ使用する
		CurveEditorState curveState_{};
		// カーブ生成の設定、キー時刻は0~1に制限する
		CurveGeneratorState generatorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleSizeOverLifetimeModule, "SizeOverLifetime");
} // Engine
