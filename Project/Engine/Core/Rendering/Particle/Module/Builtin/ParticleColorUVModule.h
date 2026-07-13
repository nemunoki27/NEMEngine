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
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleColorUVModule() = default;
		~ParticleColorUVModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// UV座標の更新方法
		ParticleUVUpdateType updateType_ = ParticleUVUpdateType::Lerp;
		// UV座標の始点と終点
		Vector2 startOffset_ = Vector2::AnyInit(0.0f);
		Vector2 endOffset_ = Vector2::AnyInit(0.0f);
		// 1秒あたりのUV移動量
		Vector2 scrollSpeed_ = Vector2::AnyInit(0.0f);
		// UV座標のイージングとループ
		EasingType offsetEasingType_ = EasingType::EaseOutSine;
		ParticleLoopSettings offsetLoop_{};
		// UV座標のカーブ設定
		bool useOffsetCurve_ = false;
		CurveVector3 offsetCurve_{};
		CurveEditorState offsetCurveState_{};
		CurveGeneratorState offsetGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };

		// UVスケールの始点と終点
		Vector2 startScale_ = Vector2::AnyInit(1.0f);
		Vector2 endScale_ = Vector2::AnyInit(1.0f);
		// UVスケールのイージングとループ
		EasingType scaleEasingType_ = EasingType::EaseOutSine;
		ParticleLoopSettings scaleLoop_{};
		// UVスケールのカーブ設定
		bool useScaleCurve_ = false;
		CurveVector3 scaleCurve_{};
		CurveEditorState scaleCurveState_{};
		CurveGeneratorState scaleGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };

		// UV回転の始点と終点
		float startRotation_ = 0.0f;
		float endRotation_ = 0.0f;
		// UV回転の中心
		Vector2 pivot_ = Vector2::AnyInit(0.5f);
		// UV回転のイージングとループ
		EasingType rotationEasingType_ = EasingType::EaseOutSine;
		ParticleLoopSettings rotationLoop_{};
		// UV回転のカーブ設定
		bool useRotationCurve_ = false;
		CurveFloat rotationCurve_{};
		CurveEditorState rotationCurveState_{};
		CurveGeneratorState rotationGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };

		//--------- functions ----------------------------------------------------

		// UV座標の編集UIを描画する
		bool DrawOffsetSettings();
		// UVスケールの編集UIを描画する
		bool DrawScaleSettings();
		// UV回転の編集UIを描画する
		bool DrawRotationSettings();
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleColorUVModule, "ColorUV");
} // Engine
