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
	//	ParticleColorUVModule class
	//	カラーテクスチャのUV行列を寿命の進行度で制御する
	//============================================================================
	// UV座標の更新方法
	enum class ParticleUVUpdateType :
		uint8_t {

		Lerp,
		Scroll,
	};

	class ParticleColorUVModule :
		public IParticleModule {
	public:

		// 保存と実行に使う設定
		struct Settings {

			// UV座標の更新方法
			ParticleUVUpdateType updateType = ParticleUVUpdateType::Lerp;
			// UV座標の始点と終点
			Vector2 startOffset = Vector2::AnyInit(0.0f);
			Vector2 endOffset = Vector2::AnyInit(0.0f);
			// 1秒あたりのUV移動量
			Vector2 scrollSpeed = Vector2::AnyInit(0.0f);
			// UV座標のイージングとループ
			EasingType offsetEasingType = EasingType::EaseOutSine;
			ParticleLoopSettings offsetLoop{};
			// UV座標のカーブ設定
			bool useOffsetCurve = false;
			CurveVector3 offsetCurve{};

			// UVスケールの始点と終点
			Vector2 startScale = Vector2::AnyInit(1.0f);
			Vector2 endScale = Vector2::AnyInit(1.0f);
			// UVスケールのイージングとループ
			EasingType scaleEasingType = EasingType::EaseOutSine;
			ParticleLoopSettings scaleLoop{};
			// UVスケールのカーブ設定
			bool useScaleCurve = false;
			CurveVector3 scaleCurve{};

			// UV回転の始点と終点
			float startRotation = 0.0f;
			float endRotation = 0.0f;
			// UV回転の中心
			Vector2 pivot = Vector2::AnyInit(0.5f);
			// UV回転のイージングとループ
			EasingType rotationEasingType = EasingType::EaseOutSine;
			ParticleLoopSettings rotationLoop{};
			// UV回転のカーブ設定
			bool useRotationCurve = false;
			CurveFloat rotationCurve{};

		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleColorUVModule() = default;
		~ParticleColorUVModule() override = default;

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
