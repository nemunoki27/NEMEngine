#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleLoopSettings.h>
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleScaleOverLifetimeModule class
	//	寿命の進行度に応じて軸別スケールをイージング補間する
	//============================================================================
	class ParticleScaleOverLifetimeModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleScaleOverLifetimeModule() = default;
		~ParticleScaleOverLifetimeModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 軸別スケールの始点と終点
		Vector3 startScale_ = Vector3::AnyInit(1.0f);
		Vector3 endScale_ = Vector3::AnyInit(0.0f);
		// イージング
		EasingType easingType_ = EasingType::EaseOutSine;
		// 進行度のループ
		ParticleLoopSettings loop_{};

		// カーブでスケールを制御するか
		bool useCurve_ = false;
		// 進行度から軸別スケールを返すカーブ
		CurveVector3 curve_{};
		// カーブ編集の状態、編集UIでのみ使用する
		CurveEditorState curveState_{};
		// カーブ生成の設定、キー時刻は0~1に制限する
		CurveGeneratorState generatorState_{ .maxKeyTime = 1.0f };
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleScaleOverLifetimeModule, "ScaleOverLifetime");
} // Engine
