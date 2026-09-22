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
	//	ParticleSpiralMovementModule class
	//	発生時の進行方向を軸に寿命で渦移動させる
	//============================================================================
	class ParticleSpiralMovementModule :
		public IParticleModule {
	public:

		// 保存と実行に使う設定
		struct Settings {

			// 渦半径
			ParticleFloatAnimationSettings radius{};
			// 累積回転数
			ParticleFloatAnimationSettings turns{ .start = 0.0f, .end = 3.0f, .easingType = EasingType::Linear };

			// 渦の開始角度
			float startAngle = 0.0f;
			// 粒子ごとの角度差
			float particleAngleOffset = 0.0f;
			// 逆方向へ回転するか
			bool reverse = false;

		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSpiralMovementModule() = default;
		~ParticleSpiralMovementModule() override = default;

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

		// 渦軸に直交する基底
		struct SpiralBasis {

			Vector3 x{};
			Vector3 y{};
		};

		//--------- variables ----------------------------------------------------

		Settings settings_{};

		//--------- functions ----------------------------------------------------

		// 発生時の進行方向から渦の基底を作る
		SpiralBasis CalculateBasis(const Particle& particle) const;
		// 粒子ごとの開始角度を取得する
		float CalculateStartAngle(uint32_t particleID) const;
		// 寿命進行度から渦の位置差分を取得する
		Vector3 CalculateOffset(const SpiralBasis& basis, float particleStartAngle, float rawT) const;
	};

} // Engine
