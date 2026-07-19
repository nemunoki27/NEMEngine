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
	//	ParticleSpiralMovementModule class
	//	発生時の進行方向を軸に寿命で渦移動させる
	//============================================================================
	class ParticleSpiralMovementModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSpiralMovementModule() = default;
		~ParticleSpiralMovementModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

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
		// 渦軸に直交する基底
		struct SpiralBasis {

			Vector3 x{};
			Vector3 y{};
		};

		//--------- variables ----------------------------------------------------

		// 渦半径
		FloatAnimationSettings radius_{};
		// 累積回転数
		FloatAnimationSettings turns_{ .start = 0.0f, .end = 3.0f, .easingType = EasingType::Linear };

		// 渦の開始角度
		float startAngle_ = 0.0f;
		// 粒子ごとの角度差
		float particleAngleOffset_ = 0.0f;
		// 逆方向へ回転するか
		bool reverse_ = false;

		//--------- functions ----------------------------------------------------

		// 寿命アニメーション設定を読み込む
		void ReadAnimationSettings(const nlohmann::json& in, FloatAnimationSettings& settings);
		// 寿命アニメーション設定を書き出す
		nlohmann::json WriteAnimationSettings(const FloatAnimationSettings& settings) const;
		// 寿命進行度から値を取得する
		float EvaluateAnimation(const FloatAnimationSettings& settings, float rawT) const;
		// 発生時の進行方向から渦の基底を作る
		SpiralBasis CalculateBasis(const Particle& particle) const;
		// 粒子ごとの開始角度を取得する
		float CalculateStartAngle(uint32_t particleID) const;
		// 寿命進行度から渦の位置差分を取得する
		Vector3 CalculateOffset(const SpiralBasis& basis, float particleStartAngle, float rawT) const;
		// 寿命アニメーション設定を編集する
		bool DrawAnimationSettings(const char* header, const char* id,
			const char* startLabel, const char* endLabel,
			FloatAnimationSettings& settings, float minValue, float maxValue);
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleSpiralMovementModule, "SpiralMovement");
} // Engine
