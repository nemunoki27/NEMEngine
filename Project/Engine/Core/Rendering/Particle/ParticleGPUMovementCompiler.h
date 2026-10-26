#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUProgram.h"

namespace Engine {

	// 移動Moduleの定数と寿命テーブルを作る
	void CompileParticleGPUMovement(IParticleModule& module, ParticleGPUInstruction& instruction, ParticleGPUProgram& program);
}
