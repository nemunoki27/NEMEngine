#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUProgram.h"

// c++
#include <functional>

namespace Engine {

	using ParticleGPUCurveEvaluator = std::function<ParticleGPUCurveSample(float)>;

	// CPUの評価結果から誤差を検査した共有テーブルを作る
	void CompileParticleGPUCurve(const nlohmann::json& parameters, const ParticleGPUCurveEvaluator& evaluate,
		ParticleGPUInstruction& instruction, ParticleGPUProgram& program);
}
