#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleLoopSettings.h>
#include <Engine/Core/Rendering/Particle/ParticleValue.h>
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>
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
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleRotationModule() = default;
		~ParticleRotationModule() override = default;

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

		//--------- variables ----------------------------------------------------

		// 回転の更新方法
		ParticleRotationMode mode_ = ParticleRotationMode::Fixed;
		// 角度の入力形式
		ParticleRotationValueType valueType_ = ParticleRotationValueType::Euler;

		// 固定角度、Eulerは度数法、Quaternionは軸と角度で保持する
		ParticleValue<Vector3> fixedAngle_{ Vector3::AnyInit(0.0f) };
		Vector3 fixedAxis_ = Vector3(1.0f, 0.0f, 0.0f);
		ParticleValue<float> fixedQuaternionAngle_{ 0.0f };

		// 発生時に加算する角度、Eulerは度数法、Quaternionは軸と角度で保持する
		ParticleValue<Vector3> addAngle_{ Vector3::AnyInit(0.0f) };
		Vector3 addAxis_ = Vector3(1.0f, 0.0f, 0.0f);
		ParticleValue<float> addQuaternionAngle_{ 0.0f };
		// 回転速度の更新方法
		ParticleRotationSpeedMode speedMode_ = ParticleRotationSpeedMode::Constant;
		// 1秒あたりの回転角度、度数法
		ParticleValue<Vector3> rotationSpeed_{ Vector3::AnyInit(0.0f) };
		Vector3 rotationSpeedAxis_ = Vector3(1.0f, 0.0f, 0.0f);
		ParticleValue<float> rotationQuaternionSpeed_{ 0.0f };
		// 寿命に応じて変化する回転速度の始点と終点
		Vector3 startSpeed_ = Vector3::AnyInit(0.0f);
		Vector3 endSpeed_ = Vector3::AnyInit(0.0f);
		Vector3 startSpeedAxis_ = Vector3(1.0f, 0.0f, 0.0f);
		Vector3 endSpeedAxis_ = Vector3(1.0f, 0.0f, 0.0f);
		float startQuaternionSpeed_ = 0.0f;
		float endQuaternionSpeed_ = 0.0f;
		EasingType speedEasingType_ = EasingType::EaseOutSine;
		ParticleLoopSettings speedLoop_{};
		bool useSpeedCurve_ = false;
		CurveVector3 speedCurve_{};
		CurveQuaternion speedQuaternionCurve_{};
		CurveEditorState speedCurveState_{};
		CurveEditorState speedQuaternionCurveState_{};
		CurveGeneratorState speedGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		CurveGeneratorState speedQuaternionGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };

		// 寿命に応じて補間する角度の始点と終点、度数法
		Vector3 startAngle_ = Vector3::AnyInit(0.0f);
		Vector3 endAngle_ = Vector3::AnyInit(0.0f);
		// Quaternion補間の始点と終点、軸と角度で保持する
		Vector3 startAxis_ = Vector3(1.0f, 0.0f, 0.0f);
		Vector3 endAxis_ = Vector3(1.0f, 0.0f, 0.0f);
		float startQuaternionAngle_ = 0.0f;
		float endQuaternionAngle_ = 0.0f;
		EasingType angleEasingType_ = EasingType::EaseOutSine;
		ParticleLoopSettings angleLoop_{};
		bool useAngleCurve_ = false;
		CurveVector3 angleCurve_{};
		CurveQuaternion quaternionCurve_{};
		CurveEditorState angleCurveState_{};
		CurveEditorState quaternionCurveState_{};
		CurveGeneratorState angleGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		CurveGeneratorState quaternionGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };

		//--------- functions ----------------------------------------------------

		// 角度加算の編集UIを描画する
		bool DrawAdditiveSettings();
		// 角度補間の編集UIを描画する
		bool DrawInterpolationSettings();
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleRotationModule, "Rotation");
} // Engine
