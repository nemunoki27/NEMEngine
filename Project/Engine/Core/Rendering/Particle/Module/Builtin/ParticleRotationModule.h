#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleLoopSettings.h>
#include <Engine/Core/Rendering/Particle/ParticleValue.h>
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleRotationModule class
	//	固定角度と角度加算と角度補間で回転を制御する
	//============================================================================
	// 回転の更新方法
	enum class ParticleRotationMode :
		uint8_t {

		Fixed,
		Additive,
		Interpolate,
	};

	// 角度加算の回転速度の更新方法
	enum class ParticleRotationSpeedMode :
		uint8_t {

		Constant,
		OverLifetime,
	};

	// 角度の入力形式
	enum class ParticleRotationValueType :
		uint8_t {

		Euler,
		Quaternion,
	};

	class ParticleRotationModule :
		public IParticleModule {
	public:

		// 保存と実行に使う設定
		struct Settings {

			// 回転の更新方法
			ParticleRotationMode mode = ParticleRotationMode::Fixed;
			// 角度の入力形式
			ParticleRotationValueType valueType = ParticleRotationValueType::Euler;

			// 固定角度、Eulerは度数法、Quaternionは軸と角度で保持する
			ParticleValue<Vector3> fixedAngle{ Vector3::AnyInit(0.0f) };
			Vector3 fixedAxis = Vector3(1.0f, 0.0f, 0.0f);
			ParticleValue<float> fixedQuaternionAngle{ 0.0f };

			// 発生時に加算する角度、Eulerは度数法、Quaternionは軸と角度で保持する
			ParticleValue<Vector3> addAngle{ Vector3::AnyInit(0.0f) };
			Vector3 addAxis = Vector3(1.0f, 0.0f, 0.0f);
			ParticleValue<float> addQuaternionAngle{ 0.0f };
			// 回転速度の更新方法
			ParticleRotationSpeedMode speedMode = ParticleRotationSpeedMode::Constant;
			// 1秒あたりの回転角度、度数法
			ParticleValue<Vector3> rotationSpeed{ Vector3::AnyInit(0.0f) };
			Vector3 rotationSpeedAxis = Vector3(1.0f, 0.0f, 0.0f);
			ParticleValue<float> rotationQuaternionSpeed{ 0.0f };
			// 寿命に応じて変化する回転速度の始点と終点
			Vector3 startSpeed = Vector3::AnyInit(0.0f);
			Vector3 endSpeed = Vector3::AnyInit(0.0f);
			Vector3 startSpeedAxis = Vector3(1.0f, 0.0f, 0.0f);
			Vector3 endSpeedAxis = Vector3(1.0f, 0.0f, 0.0f);
			float startQuaternionSpeed = 0.0f;
			float endQuaternionSpeed = 0.0f;
			EasingType speedEasingType = EasingType::EaseOutSine;
			ParticleLoopSettings speedLoop{};
			bool useSpeedCurve = false;
			CurveVector3 speedCurve{};
			CurveQuaternion speedQuaternionCurve{};

			// 寿命に応じて補間する角度の始点と終点、度数法
			Vector3 startAngle = Vector3::AnyInit(0.0f);
			Vector3 endAngle = Vector3::AnyInit(0.0f);
			// Quaternion補間の始点と終点、軸と角度で保持する
			Vector3 startAxis = Vector3(1.0f, 0.0f, 0.0f);
			Vector3 endAxis = Vector3(1.0f, 0.0f, 0.0f);
			float startQuaternionAngle = 0.0f;
			float endQuaternionAngle = 0.0f;
			EasingType angleEasingType = EasingType::EaseOutSine;
			ParticleLoopSettings angleLoop{};
			bool useAngleCurve = false;
			CurveVector3 angleCurve{};
			CurveQuaternion quaternionCurve{};

		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleRotationModule() = default;
		~ParticleRotationModule() override = default;

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

		//--------- variables ----------------------------------------------------

		Settings settings_{};

	};

} // Engine
