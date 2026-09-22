#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleFloatAnimationSettings.h>
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticlePendulumMovementModule class
	//	発生時の進行方向を軸に寿命で振り子移動させる
	//============================================================================
	class ParticlePendulumMovementModule :
		public IParticleModule {
	public:

		// 保存と実行に使う設定
		struct Settings {

			// 振り子の長さ
			ParticleFloatAnimationSettings length{ .start = 1.0f, .end = 1.0f };
			// 最大振れ角
			ParticleFloatAnimationSettings maxAngle{ .start = 30.0f, .end = 30.0f };
			// 累積振動回数
			ParticleFloatAnimationSettings cycles{ .start = 0.0f, .end = 2.0f, .easingType = EasingType::Linear };

			// 振り子平面の角度
			float planeAngle = 0.0f;
			// 振動の開始位相
			float startPhase = 0.0f;
			// 粒子ごとの位相差
			float particlePhaseOffset = 0.0f;
			// 円弧方向の移動量
			float arcStrength = 1.0f;
			// 逆方向へ振動するか
			bool reverse = false;

		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticlePendulumMovementModule() = default;
		~ParticlePendulumMovementModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;

		ParticleModuleExecutionMode GetSpawnExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnSpawn(Particle& particle) override;
		void OnUpdate(Particle& particle, float deltaTime) override;

		//--------- accessor -----------------------------------------------------

		const Settings& GetSettings() const { return settings_; }
		void SetSettings(const Settings& settings) { settings_ = settings; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 振り子軸と振動平面の基底
		struct PendulumBasis {

			Vector3 axis{};
			Vector3 x{};
			Vector3 y{};
		};

		//--------- variables ----------------------------------------------------

		Settings settings_{};

		//--------- functions ----------------------------------------------------

		// 発生時の進行方向から振り子の基底を作る
		PendulumBasis CalculateBasis(const Particle& particle) const;
		// 粒子ごとの開始位相を取得する
		float CalculateStartPhase(uint32_t particleID) const;
		// 寿命進行度から振り子の位置差分を取得する
		Vector3 CalculateOffset(const PendulumBasis& basis, float particleStartPhase, float rawT) const;
	};

} // Engine
