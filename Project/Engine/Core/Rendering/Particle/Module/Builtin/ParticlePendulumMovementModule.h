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
	//	ParticlePendulumMovementModule class
	//	発生時の進行方向を軸に寿命で振り子移動させる
	//============================================================================
	class ParticlePendulumMovementModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticlePendulumMovementModule() = default;
		~ParticlePendulumMovementModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui();

		ParticleModuleExecutionMode GetSpawnExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnSpawn(Particle& particle) override;
		void OnUpdate(Particle& particle, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// float値の寿命アニメーション設定
		struct FloatAnimationSettings {

			float start = 0.0f;
			float end = 1.0f;
			EasingType easingType = EasingType::EaseOutSine;
			ParticleLoopSettings loop{};
			bool useCurve = false;
			CurveFloat curve{};
			CurveEditorState curveState{};
			CurveGeneratorState generatorState{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		};
		// 振り子軸と振動平面の基底
		struct PendulumBasis {

			Vector3 axis{};
			Vector3 x{};
			Vector3 y{};
		};

		//--------- variables ----------------------------------------------------

		// 振り子の長さ
		FloatAnimationSettings length_{ .start = 1.0f, .end = 1.0f };
		// 最大振れ角
		FloatAnimationSettings maxAngle_{ .start = 30.0f, .end = 30.0f };
		// 累積振動回数
		FloatAnimationSettings cycles_{ .start = 0.0f, .end = 2.0f, .easingType = EasingType::Linear };

		// 振り子平面の角度
		float planeAngle_ = 0.0f;
		// 振動の開始位相
		float startPhase_ = 0.0f;
		// 粒子ごとの位相差
		float particlePhaseOffset_ = 0.0f;
		// 円弧方向の移動量
		float arcStrength_ = 1.0f;
		// 逆方向へ振動するか
		bool reverse_ = false;

		//--------- functions ----------------------------------------------------

		// 寿命アニメーション設定を読み込む
		void ReadAnimationSettings(const nlohmann::json& in, FloatAnimationSettings& settings);
		// 寿命アニメーション設定を書き出す
		nlohmann::json WriteAnimationSettings(const FloatAnimationSettings& settings) const;
		// 寿命進行度から値を取得する
		float EvaluateAnimation(const FloatAnimationSettings& settings, float rawT) const;
		// 発生時の進行方向から振り子の基底を作る
		PendulumBasis CalculateBasis(const Particle& particle) const;
		// 粒子ごとの開始位相を取得する
		float CalculateStartPhase(uint32_t particleID) const;
		// 寿命進行度から振り子の位置差分を取得する
		Vector3 CalculateOffset(const PendulumBasis& basis, float particleStartPhase, float rawT) const;
		// 寿命アニメーション設定を編集する
		bool DrawAnimationSettings(const char* header, const char* id,
			const char* startLabel, const char* endLabel,
			FloatAnimationSettings& settings, float minValue, float maxValue);
	};

} // Engine
