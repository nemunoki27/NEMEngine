#pragma once

#include "IParticleModuleDrawer.h"
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorUVModule.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

namespace Engine {

	//============================================================================
	//	ParticleColorUVModuleDrawer class
	//	モジュールの編集表示とカーブ選択を所有する
	//============================================================================
	class ParticleColorUVModuleDrawer : public IParticleModuleDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		bool Draw(IParticleModule& module) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		// カーブの選択と生成画面の状態
		CurveEditorState offsetCurveState_{};
		CurveGeneratorState offsetGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		CurveEditorState scaleCurveState_{};
		CurveGeneratorState scaleGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		CurveEditorState rotationCurveState_{};
		CurveGeneratorState rotationGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };

		//--------- functions ----------------------------------------------------

		// UV座標の編集UIを描画する
		bool DrawOffsetSettings(ParticleColorUVModule::Settings& settings);
		// UVスケールの編集UIを描画する
		bool DrawScaleSettings(ParticleColorUVModule::Settings& settings);
		// UV回転の編集UIを描画する
		bool DrawRotationSettings(ParticleColorUVModule::Settings& settings);
	};
}
