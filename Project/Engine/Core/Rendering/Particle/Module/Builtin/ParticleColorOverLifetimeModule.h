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
	//	ParticleColorOverLifetimeModule class
	//	寿命の進行度に応じて色をイージング補間する
	//============================================================================
	class ParticleColorOverLifetimeModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleColorOverLifetimeModule() = default;
		~ParticleColorOverLifetimeModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 発生時の色に掛ける色の始点と終点
		Color4 startColor_ = Color4::White();
		Color4 endColor_ = Color4(1.0f, 1.0f, 1.0f, 0.0f);
		// イージング
		EasingType easingType_ = EasingType::EaseOutSine;
		// 進行度のループ
		ParticleLoopSettings loop_{};

		// カーブで色を制御するか
		bool useCurve_ = false;
		// 進行度から色を返すカーブ
		CurveColor4 curve_{};
		// カーブ編集の状態、編集UIでのみ使用する
		CurveEditorState curveState_{};
		// カーブ生成の設定、キー時刻は0~1に制限する
		CurveGeneratorState generatorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleColorOverLifetimeModule, "ColorOverLifetime");
} // Engine
