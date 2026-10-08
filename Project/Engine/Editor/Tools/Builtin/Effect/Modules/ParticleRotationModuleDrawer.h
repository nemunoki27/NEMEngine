#pragma once

#include "IParticleModuleDrawer.h"
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleRotationModule.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

namespace Engine {

	//============================================================================
	//	ParticleRotationModuleDrawer class
	//	モジュールの編集表示とカーブ選択を所有する
	//============================================================================
	class ParticleRotationModuleDrawer : public IParticleModuleDrawer {
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
		CurveEditorState speedCurveState_{};
		CurveEditorState speedQuaternionCurveState_{};
		CurveGeneratorState speedGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		CurveGeneratorState speedQuaternionGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		CurveEditorState angleCurveState_{};
		CurveEditorState quaternionCurveState_{};
		CurveGeneratorState angleGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		CurveGeneratorState quaternionGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };

		//--------- functions ----------------------------------------------------

		// 角度加算の編集UIを描画する
		bool DrawAdditiveSettings(ParticleRotationModule::Settings& settings);
		// 角度補間の編集UIを描画する
		bool DrawInterpolationSettings(ParticleRotationModule::Settings& settings);
	};
}
