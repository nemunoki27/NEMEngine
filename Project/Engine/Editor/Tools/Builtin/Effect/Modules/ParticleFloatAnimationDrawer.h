#pragma once

#include <Engine/Core/Rendering/Particle/Structures/ParticleFloatAnimationSettings.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

namespace Engine {

	// 数値カーブの選択と生成画面の状態
	struct ParticleFloatAnimationEditState {

		CurveEditorState curveState{};
		CurveGeneratorState generatorState{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
	};

	namespace ParticleFloatAnimationDrawer {

		// 寿命アニメーションを編集する
		bool Draw(const char* header, const char* id, const char* startLabel, const char* endLabel,
			ParticleFloatAnimationSettings& settings, ParticleFloatAnimationEditState& state, float minValue, float maxValue);
	}
}
