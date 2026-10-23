#pragma once

#include "IParticleModuleDrawer.h"
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

namespace Engine {

	//============================================================================
	//	ParticleSizeOverLifetimeModuleDrawer class
	//	モジュールの設定を編集し変更時に実行定義へ戻す
	//============================================================================
	class ParticleSizeOverLifetimeModuleDrawer : public IParticleModuleDrawer {
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
		CurveEditorState curveState_{};
		CurveGeneratorState generatorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
	};
}
