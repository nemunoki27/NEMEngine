#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUProgram.h"

namespace Engine {

	// 形状の5属性を共有曲線と任意状態へ変換する
	void CompileParticleGPUShape(IParticleModule& module, ParticleGPUProgram& program);
	// 形状Moduleのない最初のPhaseへ基本形状を設定する
	void CompileParticleGPUInitialShape(const ParticleRenderSettings& settings, ParticleGPUProgram& program);
}
