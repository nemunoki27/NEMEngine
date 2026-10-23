#pragma once

#include "IParticleModuleDrawer.h"
#include <Engine/Core/Rendering/Particle/Structures/ParticleShapeAnimationSettings.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

namespace Engine {

	//============================================================================
	//	ParticleShapeOverLifetimeModuleDrawer class
	//	形状項目の編集値とカーブ選択を所有する
	//============================================================================
	class ParticleShapeOverLifetimeModuleDrawer : public IParticleModuleDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		bool Draw(IParticleModule& module) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		struct ParameterUIState {

			CurveEditorState curveState{};
			CurveGeneratorState curveGeneratorState{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
			CurveGeneratorState alphaGeneratorState{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		};

		ParticleShapeAnimationSettings settings_{};
		bool parametersInserted_ = false;
		std::unordered_map<std::string, ParameterUIState> uiStates_{};

		// 形状パラメータの寿命カーブを編集する
		bool DrawParameter(const char* label, const char* key, float defaultValue,
			float minValue, float maxValue, float dragSpeed = 0.01f);
		// 形状パラメータの寿命カーブを編集する
		bool DrawColorParameter(const char* label, const char* key, const Color4& defaultValue);
	};
}
